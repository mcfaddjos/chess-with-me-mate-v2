// engine.h -- the computer opponent, and the rating math that adapts it to the player.
//
// Strength is set by a chess rating (Elo). A rating turns into:
//   - how many moves ahead it searches (depth),
//   - whether it looks past the end of that search for hanging pieces (quiescence),
//   - a "temperature": instead of always playing its best move, it picks among the
//     moves it considers roughly as good, weighted by score. Low-rated bots have a high
//     temperature and play loose, human-looking inaccuracies; strong bots have a low one.
// The rating labels are approximate -- the bot is tuned by feel, not calibrated against
// rated players -- which is why the adaptive loop (AdjustRating) matters more than the
// exact number: if the bot is too easy, your rating and the next bot's both climb.
//
// No graphics here; see tests/engine_tests.cpp.

#ifndef ENGINE_H
#define ENGINE_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>
#include "chessrules.h"

// ---------------------------------------------------------------- ratings

const int RATING_MIN = 100;
const int RATING_MAX = 2600;
const int RATING_START = 1000;		// "medium-low": US Chess Class E, a solid beginner
const int BOT_STRETCH = 50;		// the next bot is rated this much above you

// US Chess rating classes
inline const char* RatingClass(int elo)
{
	if (elo >= 2400) return "SENIOR MASTER";
	if (elo >= 2200) return "MASTER";
	if (elo >= 2000) return "EXPERT";
	if (elo >= 1800) return "CLASS A";
	if (elo >= 1600) return "CLASS B";
	if (elo >= 1400) return "CLASS C";
	if (elo >= 1200) return "CLASS D";
	if (elo >= 1000) return "CLASS E";
	if (elo >= 800)  return "CLASS F";
	if (elo >= 600)  return "CLASS G";
	if (elo >= 400)  return "CLASS H";
	if (elo >= 200)  return "CLASS I";
	return "CLASS J";
}

// chance the player scores against an opponent (standard Elo expectation)
inline double ExpectedScore(int player, int opponent)
{
	return 1.0 / (1.0 + std::pow(10.0, (opponent - player) / 400.0));
}

// new player rating after a game: score is 1 win, 0.5 draw, 0 loss.
// Moves fast for the first 20 games so it finds your level quickly, then settles.
inline int AdjustRating(int player, int opponent, double score, int gamesPlayed)
{
	double k = (gamesPlayed < 20) ? 40.0 : 24.0;
	int next = (int)std::lround(player + k * (score - ExpectedScore(player, opponent)));
	return std::max(RATING_MIN, std::min(RATING_MAX, next));
}

inline int BotRatingFor(int playerRating)
{
	return std::max(RATING_MIN, std::min(RATING_MAX, playerRating + BOT_STRETCH));
}

struct BotLevel
{
	int elo;
	int maxDepth;		// plies
	bool quiescence;	// resolve captures past the depth limit
	double temperature;	// centipawns; higher = looser move choice
	double thinkSeconds;	// search time budget
};

inline BotLevel LevelForElo(int elo)
{
	BotLevel l;
	l.elo = elo;
	l.maxDepth = elo < 700 ? 1 : elo < 1000 ? 2 : elo < 1300 ? 3 : elo < 1600 ? 4 : elo < 1900 ? 5 : 6;
	l.quiescence = elo >= 900;
	l.temperature = std::max(5.0, std::min(350.0, (2200.0 - elo) / 5.0));
	l.thinkSeconds = std::min(2.5, 0.25 + elo / 1500.0);
	return l;
}

// ---------------------------------------------------------------- search

class Engine
{
public:
	static const int MATE = 100000;
	static const int INF = 1000000;

	// Picks a move for the side to move. 'stop' lets the caller cancel (new game, undo);
	// the returned move is then meaningless. Returns a move with from == to if there are none.
	Move Choose(ChessRules pos, const BotLevel& level, std::atomic<bool>& stop, unsigned seed)
	{
		stopFlag = &stop;
		aborted = false;
		useQuiescence = level.quiescence;
		nodes = 0;
		auto start = std::chrono::steady_clock::now();
		deadline = start + std::chrono::milliseconds((int)(level.thinkSeconds * 1000));

		std::vector<Move> moves;
		pos.GenerateLegal(moves);
		Move none;
		if (moves.empty())
			return none;

		std::vector<int> scores(moves.size(), 0);
		// how far below the best a move may score and still be considered at all:
		int window = std::max(60, (int)(4 * level.temperature));

		for (int depth = 1; depth <= level.maxDepth; depth++)
		{
			std::vector<int> iter(moves.size(), -INF);
			int best = -INF;
			bool complete = true;
			for (size_t i = 0; i < moves.size(); i++)
			{
				pos.MakeMove(moves[i]);
				int alpha = (best == -INF) ? -INF : best - window;
				int s = -Search(pos, depth - 1, -INF, -alpha, 1);
				pos.UnmakeMove();
				if (aborted)
				{
					complete = false;
					break;
				}
				// a result at or below alpha only means "outside the window" (the search returns
				// the bound itself), so drop it rather than let it look like a close alternative
				if (alpha != -INF && s <= alpha)
					s = -INF;
				iter[i] = s;
				best = std::max(best, s);
			}
			if (!complete && depth > 1)
				break;		// out of time: keep the last finished depth
			scores = iter;
			if (aborted)
				break;

			// search the best moves first next time (better cutoffs):
			std::vector<size_t> order(moves.size());
			for (size_t i = 0; i < order.size(); i++)
				order[i] = i;
			std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return scores[a] > scores[b]; });
			std::vector<Move> m2;
			std::vector<int> s2;
			for (size_t i : order)
			{
				m2.push_back(moves[i]);
				s2.push_back(scores[i]);
			}
			moves = m2;
			scores = s2;
		}

		return Pick(moves, scores, level.temperature, window, seed);
	}

	// static evaluation, from the point of view of the side to move
	int Evaluate(const ChessRules& pos) const
	{
		static const int value[7] = { 0, 100, 320, 330, 500, 900, 0 };
		int mg = 0, eg = 0, phase = 0;
		for (int sq = 0; sq < 64; sq++)
		{
			int8_t pc = pos.board[sq];
			if (pc == 0)
				continue;
			int t = TypeOf(pc);
			bool white = pc > 0;
			// tables are written rank 8 first, as you'd see the board from white's side:
			int idx = white ? (7 - RankOf(sq)) * 8 + FileOf(sq) : RankOf(sq) * 8 + FileOf(sq);
			int sign = white ? 1 : -1;
			int base = value[t];
			switch (t)
			{
			case PAWN:	base += PawnTable[idx]; break;
			case KNIGHT:	base += KnightTable[idx]; phase += 1; break;
			case BISHOP:	base += BishopTable[idx]; phase += 1; break;
			case ROOK:	base += RookTable[idx]; phase += 2; break;
			case QUEEN:	base += QueenTable[idx]; phase += 4; break;
			}
			if (t == KING)
			{
				mg += sign * KingMidTable[idx];
				eg += sign * KingEndTable[idx];
			}
			else
			{
				mg += sign * base;
				eg += sign * base;
			}
		}
		phase = std::min(phase, 24);
		int score = (mg * phase + eg * (24 - phase)) / 24;	// blend king safety -> king activity
		return pos.side == SIDE_WHITE ? score : -score;
	}

	long long Nodes() const { return nodes; }

private:
	std::atomic<bool>* stopFlag = nullptr;
	std::chrono::steady_clock::time_point deadline;
	bool aborted = false;
	bool useQuiescence = true;
	long long nodes = 0;

	bool OutOfTime()
	{
		if ((nodes & 1023) == 0)
			if (stopFlag->load() || std::chrono::steady_clock::now() > deadline)
				aborted = true;
		return aborted;
	}

	// captures and promotions first, biggest victim / smallest attacker first
	static int OrderScore(const ChessRules& pos, const Move& m)
	{
		static const int value[7] = { 0, 1, 3, 3, 5, 9, 10 };
		int s = 0;
		if (m.flags & MF_CAPTURE)
		{
			int victim = (m.flags & MF_ENPASSANT) ? PAWN : TypeOf(pos.board[m.to]);
			s += 100 + value[victim] * 10 - value[TypeOf(pos.board[m.from])];
		}
		if (m.flags & MF_PROMOTION)
			s += 90 + value[m.promo];
		return s;
	}

	static void OrderMoves(const ChessRules& pos, std::vector<Move>& moves)
	{
		std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
			return OrderScore(pos, a) > OrderScore(pos, b);
		});
	}

	int Search(ChessRules& pos, int depth, int alpha, int beta, int ply)
	{
		nodes++;
		if (OutOfTime())
			return 0;
		if (pos.halfmove >= 100 || pos.InsufficientMaterial())
			return 0;
		if (depth <= 0)
			return useQuiescence ? Quiesce(pos, alpha, beta, ply) : Evaluate(pos);

		std::vector<Move> moves;
		pos.GeneratePseudoLegal(moves);
		OrderMoves(pos, moves);

		int mover = pos.side;
		int legal = 0;
		for (const Move& m : moves)
		{
			pos.MakeMove(m);
			if (pos.InCheck(mover))
			{
				pos.UnmakeMove();
				continue;
			}
			legal++;
			int s = -Search(pos, depth - 1, -beta, -alpha, ply + 1);
			pos.UnmakeMove();
			if (aborted)
				return 0;
			if (s >= beta)
				return s;
			alpha = std::max(alpha, s);
		}

		if (legal == 0)
			return pos.InCheck(mover) ? -MATE + ply : 0;	// checkmated (sooner is worse) or stalemate
		return alpha;
	}

	int Quiesce(ChessRules& pos, int alpha, int beta, int ply)
	{
		nodes++;
		if (OutOfTime())
			return 0;
		int standPat = Evaluate(pos);
		if (standPat >= beta)
			return standPat;
		alpha = std::max(alpha, standPat);

		std::vector<Move> moves;
		pos.GeneratePseudoLegal(moves);
		OrderMoves(pos, moves);
		int mover = pos.side;
		for (const Move& m : moves)
		{
			if (!(m.flags & (MF_CAPTURE | MF_PROMOTION)))
				continue;
			pos.MakeMove(m);
			if (pos.InCheck(mover))
			{
				pos.UnmakeMove();
				continue;
			}
			int s = -Quiesce(pos, -beta, -alpha, ply + 1);
			pos.UnmakeMove();
			if (aborted)
				return 0;
			if (s >= beta)
				return s;
			alpha = std::max(alpha, s);
		}
		return alpha;
	}

	// softmax over the moves within 'window' of the best
	static Move Pick(const std::vector<Move>& moves, const std::vector<int>& scores, double temperature, int window, unsigned seed)
	{
		int best = *std::max_element(scores.begin(), scores.end());
		std::vector<double> weights(moves.size(), 0.0);
		for (size_t i = 0; i < moves.size(); i++)
			if (scores[i] > -INF && scores[i] >= best - window)
				weights[i] = std::exp((scores[i] - best) / temperature);
		std::mt19937 rng(seed);
		std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
		return moves[dist(rng)];
	}

	// piece-square tables (Tomasz Michniewski's "simplified evaluation function")
	static constexpr int PawnTable[64] = {
		 0,  0,  0,  0,  0,  0,  0,  0,
		50, 50, 50, 50, 50, 50, 50, 50,
		10, 10, 20, 30, 30, 20, 10, 10,
		 5,  5, 10, 25, 25, 10,  5,  5,
		 0,  0,  0, 20, 20,  0,  0,  0,
		 5, -5,-10,  0,  0,-10, -5,  5,
		 5, 10, 10,-20,-20, 10, 10,  5,
		 0,  0,  0,  0,  0,  0,  0,  0 };
	static constexpr int KnightTable[64] = {
		-50,-40,-30,-30,-30,-30,-40,-50,
		-40,-20,  0,  0,  0,  0,-20,-40,
		-30,  0, 10, 15, 15, 10,  0,-30,
		-30,  5, 15, 20, 20, 15,  5,-30,
		-30,  0, 15, 20, 20, 15,  0,-30,
		-30,  5, 10, 15, 15, 10,  5,-30,
		-40,-20,  0,  5,  5,  0,-20,-40,
		-50,-40,-30,-30,-30,-30,-40,-50 };
	static constexpr int BishopTable[64] = {
		-20,-10,-10,-10,-10,-10,-10,-20,
		-10,  0,  0,  0,  0,  0,  0,-10,
		-10,  0,  5, 10, 10,  5,  0,-10,
		-10,  5,  5, 10, 10,  5,  5,-10,
		-10,  0, 10, 10, 10, 10,  0,-10,
		-10, 10, 10, 10, 10, 10, 10,-10,
		-10,  5,  0,  0,  0,  0,  5,-10,
		-20,-10,-10,-10,-10,-10,-10,-20 };
	static constexpr int RookTable[64] = {
		 0,  0,  0,  0,  0,  0,  0,  0,
		 5, 10, 10, 10, 10, 10, 10,  5,
		-5,  0,  0,  0,  0,  0,  0, -5,
		-5,  0,  0,  0,  0,  0,  0, -5,
		-5,  0,  0,  0,  0,  0,  0, -5,
		-5,  0,  0,  0,  0,  0,  0, -5,
		-5,  0,  0,  0,  0,  0,  0, -5,
		 0,  0,  0,  5,  5,  0,  0,  0 };
	static constexpr int QueenTable[64] = {
		-20,-10,-10, -5, -5,-10,-10,-20,
		-10,  0,  0,  0,  0,  0,  0,-10,
		-10,  0,  5,  5,  5,  5,  0,-10,
		 -5,  0,  5,  5,  5,  5,  0, -5,
		  0,  0,  5,  5,  5,  5,  0, -5,
		-10,  5,  5,  5,  5,  5,  0,-10,
		-10,  0,  5,  0,  0,  0,  0,-10,
		-20,-10,-10, -5, -5,-10,-10,-20 };
	static constexpr int KingMidTable[64] = {
		-30,-40,-40,-50,-50,-40,-40,-30,
		-30,-40,-40,-50,-50,-40,-40,-30,
		-30,-40,-40,-50,-50,-40,-40,-30,
		-30,-40,-40,-50,-50,-40,-40,-30,
		-20,-30,-30,-40,-40,-30,-30,-20,
		-10,-20,-20,-20,-20,-20,-20,-10,
		 20, 20,  0,  0,  0,  0, 20, 20,
		 20, 30, 10,  0,  0, 10, 30, 20 };
	static constexpr int KingEndTable[64] = {
		-50,-40,-30,-20,-20,-30,-40,-50,
		-30,-20,-10,  0,  0,-10,-20,-30,
		-30,-10, 20, 30, 30, 20,-10,-30,
		-30,-10, 30, 40, 40, 30,-10,-30,
		-30,-10, 30, 40, 40, 30,-10,-30,
		-30,-10, 20, 30, 30, 20,-10,-30,
		-30,-30,  0,  0,  0,  0,-30,-30,
		-50,-30,-30,-30,-30,-30,-30,-50 };
};

#endif	// ENGINE_H
