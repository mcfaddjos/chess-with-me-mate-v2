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
const int CHALLENGE_STRETCH = 250;	// ...or this much, in a challenge game

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

inline int BotRatingFor(int playerRating, bool challenge = false)
{
	int stretch = challenge ? CHALLENGE_STRETCH : BOT_STRETCH;
	return std::max(RATING_MIN, std::min(RATING_MAX, playerRating + stretch));
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

	// The engine's honest best move (no temperature), iterative deepening up to maxDepth within
	// 'seconds'. Returns the score for the side to move; depthUsed is the last completed depth.
	// Depths up to minDepth always finish (only 'stop' can cut them short) so the result never
	// depends on how busy the machine is -- a time budget only limits the extra depth beyond it.
	int SearchBest(ChessRules& pos, int maxDepth, double seconds, std::atomic<bool>& stop, Move& best, int& depthUsed,
		int minDepth = 1)
	{
		Begin(stop, seconds);
		std::vector<Move> moves;
		pos.GenerateLegal(moves);
		best = Move();
		depthUsed = 0;
		if (moves.empty())
			return pos.InCheck(pos.side) ? -MATE : 0;
		OrderMoves(pos, moves);

		int bestScore = -INF;
		for (int depth = 1; depth <= maxDepth; depth++)
		{
			enforceDeadline = depth > minDepth;
			int alpha = -INF, iterScore = -INF;
			size_t iterBest = 0;
			bool complete = true;
			for (size_t i = 0; i < moves.size(); i++)
			{
				pos.MakeMove(moves[i]);
				int s = -Search(pos, depth - 1, -INF, -alpha, 1);
				pos.UnmakeMove();
				if (aborted)
				{
					complete = false;
					break;
				}
				if (s > iterScore)
				{
					iterScore = s;
					iterBest = i;
				}
				alpha = std::max(alpha, s);
			}
			if (!complete)
				break;
			best = moves[iterBest];
			bestScore = iterScore;
			depthUsed = depth;
			std::rotate(moves.begin(), moves.begin() + iterBest, moves.begin() + iterBest + 1);	// best first next time
		}
		if (depthUsed == 0)		// ran out of time before even depth 1 finished
		{
			best = moves[0];
			depthUsed = 1;
			bestScore = ScoreMove(pos, best, 1, stop);
		}
		return bestScore;
	}

	// score of playing m here, for the side to move, searched to 'depth' (no time limit beyond 'stop')
	int ScoreMove(ChessRules& pos, const Move& m, int depth, std::atomic<bool>& stop)
	{
		Begin(stop, 60.0);
		pos.MakeMove(m);
		int s = -Search(pos, depth - 1, -INF, INF, 1);
		pos.UnmakeMove();
		return s;
	}

private:
	void Begin(std::atomic<bool>& stop, double seconds)
	{
		stopFlag = &stop;
		aborted = false;
		enforceDeadline = true;
		useQuiescence = true;
		nodes = 0;
		deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
	}

	std::atomic<bool>* stopFlag = nullptr;
	std::chrono::steady_clock::time_point deadline;
	bool aborted = false;
	bool useQuiescence = true;
	long long nodes = 0;

	bool enforceDeadline = true;	// false while SearchBest is still inside its guaranteed minimum depth

	bool OutOfTime()
	{
		if ((nodes & 1023) == 0)
			if (stopFlag->load() || (enforceDeadline && std::chrono::steady_clock::now() > deadline))
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

// ---------------------------------------------------------------- post-game analysis

// Accuracy follows lichess's published method: a score becomes a winning chance, and each
// move's accuracy comes from how much winning chance it gave away versus the engine's best.
// Verdicts use lichess's thresholds on that same drop (10% / 20% / 30%).

enum MoveVerdict
{
	VERDICT_BEST,
	VERDICT_GOOD,
	VERDICT_INACCURACY,
	VERDICT_MISTAKE,
	VERDICT_BLUNDER
};

inline const char* VerdictName(int v)
{
	static const char* names[] = { "BEST", "GOOD", "INACCURACY", "MISTAKE", "BLUNDER" };
	return names[std::max(0, std::min(4, v))];
}

// chance of winning (0-100) for the side with this centipawn score
inline double WinChance(int cp)
{
	cp = std::max(-1500, std::min(1500, cp));	// mate scores count as "totally winning"
	return 50.0 + 50.0 * (2.0 / (1.0 + std::exp(-0.00368208 * cp)) - 1.0);
}

inline double MoveAccuracy(double winBefore, double winAfter)
{
	double a = 103.1668 * std::exp(-0.04354 * std::max(0.0, winBefore - winAfter)) - 3.1669;
	return std::max(0.0, std::min(100.0, a));
}

struct MoveReview
{
	int ply = 0;			// index of this move in the game
	int moveNumber = 0;		// chess move number: 1, 2, 3...
	Move played, best, reply;	// reply = the opponent's best answer to the move you played
	std::string playedName, bestName, replyName;
	int bestScore = 0, playedScore = 0;	// centipawns, from your side
	double winBefore = 50, winAfter = 50;	// winning chances %, from your side
	double accuracy = 100;
	int verdict = VERDICT_BEST;
	int depth = 0;			// how many moves ahead the analysis looked
	int missedCapture = NO_PIECE;	// the piece the better move would have won, if that's what you missed
	bool replyPunishes = false;		// the reply to your move captures or checks
};

// why the move was a mistake, in words -- without giving away the better move itself:
// either you missed winning something, or your move handed the opponent a strong reply
inline std::string LessonCause(const MoveReview& r)
{
	static const char* names[7] = { "", "PAWN", "KNIGHT", "BISHOP", "ROOK", "QUEEN", "KING" };
	if (r.missedCapture != NO_PIECE)
		return std::string("YOU MISSED A CHANCE TO WIN A ") + names[r.missedCapture];
	if (!r.replyName.empty())
		return std::string(r.replyPunishes ? "IT ALLOWED " : "THE BOT'S BEST REPLY WAS ") + r.replyName;
	return "";
}

struct GameReport
{
	bool valid = false;
	double accuracy = 0;			// average over your moves
	int counts[5] = { 0, 0, 0, 0, 0 };	// by MoveVerdict
	std::vector<MoveReview> moves;		// every move by the side analysed
	std::vector<int> lessons;		// indexes into moves: your worst moves (up to 5), in game order
};

inline bool SameMove(const Move& a, const Move& b)
{
	return a.from == b.from && a.to == b.to && a.promo == b.promo;
}

// Replays a finished game and reviews every move 'side' made. 'progress' counts reviewed moves.
inline GameReport AnalyzeGame(const ChessRules& start, const std::vector<Move>& moves, int side,
	std::atomic<bool>& stop, std::atomic<int>& progress, double secondsPerMove = 0.35, int maxDepth = 4)
{
	GameReport report;
	ChessRules pos = start;
	Engine engine;

	for (size_t i = 0; i < moves.size(); i++)
	{
		if (stop.load())
			return report;		// valid stays false
		const Move& m = moves[i];
		if (pos.side == side)
		{
			MoveReview r;
			r.ply = (int)i;
			r.moveNumber = pos.fullmove;
			r.played = m;
			r.playedName = pos.MoveName(m);

			int depth;
			r.bestScore = engine.SearchBest(pos, maxDepth, secondsPerMove, stop, r.best, depth, 3);	// always look 3 ahead
			r.depth = depth;
			r.bestName = pos.MoveName(r.best);
			r.playedScore = SameMove(m, r.best) ? r.bestScore : engine.ScoreMove(pos, m, depth, stop);
			r.playedScore = std::min(r.playedScore, r.bestScore);	// the best move is the yardstick

			// did the better move win material that yours didn't?
			static const int worth[7] = { 0, 1, 3, 3, 5, 9, 0 };
			auto captured = [&pos](const Move& mv) -> int {
				if (!(mv.flags & MF_CAPTURE))
					return NO_PIECE;
				return (mv.flags & MF_ENPASSANT) ? (int)PAWN : TypeOf(pos.board[mv.to]);
			};
			int bestTakes = captured(r.best), playedTakes = captured(m);
			if (!SameMove(m, r.best) && bestTakes != NO_PIECE && worth[bestTakes] > worth[playedTakes] &&
				r.bestScore - r.playedScore >= 100)
				r.missedCapture = bestTakes;

			// what did the move allow?
			pos.MakeMove(m);
			std::vector<Move> replies;
			pos.GenerateLegal(replies);
			if (!replies.empty())
			{
				int d2;
				engine.SearchBest(pos, std::max(1, depth - 1), secondsPerMove / 2, stop, r.reply, d2);
				r.replyName = pos.MoveName(r.reply);
				int replier = pos.side;
				pos.MakeMove(r.reply);
				r.replyPunishes = (r.reply.flags & MF_CAPTURE) || pos.InCheck(replier ^ 1);
				pos.UnmakeMove();
			}
			pos.UnmakeMove();

			r.winBefore = WinChance(r.bestScore);
			r.winAfter = WinChance(r.playedScore);
			r.accuracy = MoveAccuracy(r.winBefore, r.winAfter);
			double drop = r.winBefore - r.winAfter;
			if (SameMove(m, r.best) || drop < 2.0)	r.verdict = VERDICT_BEST;
			else if (drop < 10.0)			r.verdict = VERDICT_GOOD;
			else if (drop < 20.0)			r.verdict = VERDICT_INACCURACY;
			else if (drop < 30.0)			r.verdict = VERDICT_MISTAKE;
			else					r.verdict = VERDICT_BLUNDER;

			report.moves.push_back(r);
			report.counts[r.verdict]++;
			progress++;
		}
		pos.MakeMove(m);
	}

	if (stop.load())
		return report;

	double total = 0;
	for (const MoveReview& r : report.moves)
		total += r.accuracy;
	report.accuracy = report.moves.empty() ? 100.0 : total / report.moves.size();

	// the lessons: your five costliest moves (inaccuracy or worse), shown in the order you played them
	std::vector<int> bad;
	for (size_t i = 0; i < report.moves.size(); i++)
		if (report.moves[i].verdict >= VERDICT_INACCURACY)
			bad.push_back((int)i);
	std::stable_sort(bad.begin(), bad.end(), [&](int a, int b) {
		const MoveReview& x = report.moves[a];
		const MoveReview& y = report.moves[b];
		return (x.winBefore - x.winAfter) > (y.winBefore - y.winAfter);
	});
	if (bad.size() > 5)
		bad.resize(5);
	std::sort(bad.begin(), bad.end());
	report.lessons = bad;
	report.valid = true;
	return report;
}

#endif	// ENGINE_H
