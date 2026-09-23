// chessrules.h -- the rules of chess, with no graphics in sight.
//
// The board is 64 signed bytes, a1 = 0 ... h8 = 63 (square = rank*8 + file).
// Positive values are white pieces, negative are black, 0 is empty.
// Legal moves are found by generating pseudo-legal moves, playing each one,
// and throwing it away if it leaves the mover's king attacked.

#ifndef CHESSRULES_H
#define CHESSRULES_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

enum PieceType
{
	NO_PIECE = 0,
	PAWN,
	KNIGHT,
	BISHOP,
	ROOK,
	QUEEN,
	KING
};

enum Side
{
	SIDE_WHITE = 0,
	SIDE_BLACK = 1
};

enum MoveFlags
{
	MF_CAPTURE   = 1 << 0,
	MF_DOUBLE    = 1 << 1,	// pawn two-step, sets the en passant square
	MF_ENPASSANT = 1 << 2,
	MF_CASTLE    = 1 << 3,
	MF_PROMOTION = 1 << 4
};

enum GameStatus
{
	STATUS_PLAYING,
	STATUS_CHECK,
	STATUS_CHECKMATE,
	STATUS_STALEMATE,
	STATUS_DRAW_FIFTY,
	STATUS_DRAW_MATERIAL
};

// castling rights bits:
const int CASTLE_WK = 1;
const int CASTLE_WQ = 2;
const int CASTLE_BK = 4;
const int CASTLE_BQ = 8;

inline int FileOf(int sq)		{ return sq & 7; }
inline int RankOf(int sq)		{ return sq >> 3; }
inline int MakeSquare(int f, int r)	{ return r * 8 + f; }
inline bool OnBoard(int f, int r)	{ return f >= 0 && f < 8 && r >= 0 && r < 8; }
inline int TypeOf(int8_t pc)		{ return pc < 0 ? -pc : pc; }
inline int SideOf(int8_t pc)		{ return pc < 0 ? SIDE_BLACK : SIDE_WHITE; }

inline std::string SquareName(int sq)
{
	std::string s;
	s += (char)('a' + FileOf(sq));
	s += (char)('1' + RankOf(sq));
	return s;
}

struct Move
{
	int8_t from = 0;
	int8_t to = 0;
	int8_t promo = NO_PIECE;	// piece type a pawn becomes, or NO_PIECE
	uint8_t flags = 0;
};

class ChessRules
{
public:
	int8_t board[64];
	int side;		// SIDE_WHITE or SIDE_BLACK to move
	int castling;		// CASTLE_* bits still available
	int epSquare;		// square a pawn can capture onto en passant, or -1
	int halfmove;		// plies since the last capture or pawn move
	int fullmove;

	ChessRules() { Reset(); }

	void Reset()
	{
		LoadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	}

	// Forsyth-Edwards Notation -- handy for tests and setting up positions:
	bool LoadFEN(const std::string& fen)
	{
		for (int i = 0; i < 64; i++)
			board[i] = 0;
		history.clear();

		size_t i = 0;
		int f = 0, r = 7;
		for (; i < fen.size() && fen[i] != ' '; i++)
		{
			char c = fen[i];
			if (c == '/')		{ f = 0; r--; continue; }
			if (c >= '1' && c <= '8')	{ f += c - '0'; continue; }
			int type = TypeFromChar(c);
			if (type == NO_PIECE || !OnBoard(f, r))
				return false;
			board[MakeSquare(f, r)] = (int8_t)((c >= 'a') ? -type : type);
			f++;
		}

		side = SIDE_WHITE;
		castling = 0;
		epSquare = -1;
		halfmove = 0;
		fullmove = 1;

		std::vector<std::string> fields;
		std::string cur;
		for (; i <= fen.size(); i++)
		{
			if (i == fen.size() || fen[i] == ' ')
			{
				if (!cur.empty())
					fields.push_back(cur);
				cur.clear();
			}
			else
				cur += fen[i];
		}

		if (fields.size() > 0)
			side = (fields[0] == "b") ? SIDE_BLACK : SIDE_WHITE;
		if (fields.size() > 1)
		{
			for (char c : fields[1])
			{
				if (c == 'K') castling |= CASTLE_WK;
				if (c == 'Q') castling |= CASTLE_WQ;
				if (c == 'k') castling |= CASTLE_BK;
				if (c == 'q') castling |= CASTLE_BQ;
			}
		}
		if (fields.size() > 2 && fields[2].size() == 2)
			epSquare = MakeSquare(fields[2][0] - 'a', fields[2][1] - '1');
		if (fields.size() > 3)
			halfmove = atoi(fields[3].c_str());
		if (fields.size() > 4)
			fullmove = atoi(fields[4].c_str());
		return true;
	}

	int KingSquare(int s) const
	{
		int8_t king = (s == SIDE_WHITE) ? KING : -KING;
		for (int sq = 0; sq < 64; sq++)
			if (board[sq] == king)
				return sq;
		return -1;
	}

	// is square sq attacked by any piece belonging to side 'by'?
	bool IsAttacked(int sq, int by) const
	{
		int f = FileOf(sq), r = RankOf(sq);
		int sign = (by == SIDE_WHITE) ? 1 : -1;

		// pawns attack diagonally forward, so look diagonally backward from sq:
		int pr = r - sign;
		for (int df = -1; df <= 1; df += 2)
			if (OnBoard(f + df, pr) && board[MakeSquare(f + df, pr)] == sign * PAWN)
				return true;

		for (int i = 0; i < 8; i++)
		{
			int nf = f + KnightDF[i], nr = r + KnightDR[i];
			if (OnBoard(nf, nr) && board[MakeSquare(nf, nr)] == sign * KNIGHT)
				return true;
		}

		for (int i = 0; i < 8; i++)
		{
			int nf = f + KingDF[i], nr = r + KingDR[i];
			if (OnBoard(nf, nr) && board[MakeSquare(nf, nr)] == sign * KING)
				return true;
		}

		// sliders: first four directions are straight (rook/queen), last four diagonal (bishop/queen):
		for (int i = 0; i < 8; i++)
		{
			int slider = (i < 4) ? ROOK : BISHOP;
			int nf = f + KingDF[i], nr = r + KingDR[i];
			while (OnBoard(nf, nr))
			{
				int8_t pc = board[MakeSquare(nf, nr)];
				if (pc != 0)
				{
					if (pc == sign * slider || pc == sign * QUEEN)
						return true;
					break;
				}
				nf += KingDF[i];
				nr += KingDR[i];
			}
		}
		return false;
	}

	bool InCheck(int s) const
	{
		int k = KingSquare(s);
		return k >= 0 && IsAttacked(k, s ^ 1);
	}

	void GeneratePseudoLegal(std::vector<Move>& moves) const
	{
		moves.clear();
		int sign = (side == SIDE_WHITE) ? 1 : -1;

		for (int sq = 0; sq < 64; sq++)
		{
			int8_t pc = board[sq];
			if (pc == 0 || SideOf(pc) != side)
				continue;

			int f = FileOf(sq), r = RankOf(sq);
			switch (TypeOf(pc))
			{
			case PAWN:
			{
				int startRank = (side == SIDE_WHITE) ? 1 : 6;
				int lastRank = (side == SIDE_WHITE) ? 7 : 0;
				int nr = r + sign;
				if (OnBoard(f, nr) && board[MakeSquare(f, nr)] == 0)
				{
					AddPawnMove(moves, sq, MakeSquare(f, nr), 0, nr == lastRank);
					int nr2 = r + 2 * sign;
					if (r == startRank && board[MakeSquare(f, nr2)] == 0)
						AddMove(moves, sq, MakeSquare(f, nr2), MF_DOUBLE);
				}
				for (int df = -1; df <= 1; df += 2)
				{
					if (!OnBoard(f + df, nr))
						continue;
					int to = MakeSquare(f + df, nr);
					if (board[to] != 0 && SideOf(board[to]) != side)
						AddPawnMove(moves, sq, to, MF_CAPTURE, nr == lastRank);
					else if (to == epSquare)
						AddMove(moves, sq, to, MF_CAPTURE | MF_ENPASSANT);
				}
				break;
			}
			case KNIGHT:
				for (int i = 0; i < 8; i++)
					AddStep(moves, sq, f + KnightDF[i], r + KnightDR[i]);
				break;
			case BISHOP:
				AddSlides(moves, sq, 4, 8);
				break;
			case ROOK:
				AddSlides(moves, sq, 0, 4);
				break;
			case QUEEN:
				AddSlides(moves, sq, 0, 8);
				break;
			case KING:
				for (int i = 0; i < 8; i++)
					AddStep(moves, sq, f + KingDF[i], r + KingDR[i]);
				AddCastles(moves, sq);
				break;
			}
		}
	}

	void GenerateLegal(std::vector<Move>& moves)
	{
		std::vector<Move> pseudo;
		GeneratePseudoLegal(pseudo);
		moves.clear();
		int mover = side;
		for (const Move& m : pseudo)
		{
			MakeMove(m);
			if (!InCheck(mover))
				moves.push_back(m);
			UnmakeMove();
		}
	}

	void MakeMove(const Move& m)
	{
		Undo u;
		u.move = m;
		u.captured = board[m.to];
		u.castling = castling;
		u.epSquare = epSquare;
		u.halfmove = halfmove;

		int8_t pc = board[m.from];
		int sign = (pc > 0) ? 1 : -1;

		if (m.flags & MF_ENPASSANT)
		{
			int capSq = m.to - 8 * sign;
			u.captured = board[capSq];
			board[capSq] = 0;
		}

		board[m.to] = (m.flags & MF_PROMOTION) ? (int8_t)(sign * m.promo) : pc;
		board[m.from] = 0;

		if (m.flags & MF_CASTLE)
		{
			int rookFrom, rookTo;
			CastleRookSquares(m, rookFrom, rookTo);
			board[rookTo] = board[rookFrom];
			board[rookFrom] = 0;
		}

		epSquare = (m.flags & MF_DOUBLE) ? (m.from + m.to) / 2 : -1;
		castling &= CastleMask(m.from) & CastleMask(m.to);
		halfmove = (TypeOf(pc) == PAWN || u.captured != 0) ? 0 : halfmove + 1;
		if (side == SIDE_BLACK)
			fullmove++;
		side ^= 1;

		history.push_back(u);
	}

	void UnmakeMove()
	{
		if (history.empty())
			return;
		Undo u = history.back();
		history.pop_back();
		const Move& m = u.move;

		side ^= 1;
		if (side == SIDE_BLACK)
			fullmove--;

		int8_t pc = board[m.to];
		int sign = (pc > 0) ? 1 : -1;
		board[m.from] = (m.flags & MF_PROMOTION) ? (int8_t)(sign * PAWN) : pc;

		if (m.flags & MF_ENPASSANT)
		{
			board[m.to] = 0;
			board[m.to - 8 * sign] = u.captured;
		}
		else
			board[m.to] = u.captured;

		if (m.flags & MF_CASTLE)
		{
			int rookFrom, rookTo;
			CastleRookSquares(m, rookFrom, rookTo);
			board[rookFrom] = board[rookTo];
			board[rookTo] = 0;
		}

		castling = u.castling;
		epSquare = u.epSquare;
		halfmove = u.halfmove;
	}

	bool CanUndo() const		{ return !history.empty(); }
	int Ply() const			{ return (int)history.size(); }	// moves made since the start/FEN
	const Move* LastMove() const	{ return history.empty() ? nullptr : &history.back().move; }

	GameStatus Status()
	{
		std::vector<Move> moves;
		GenerateLegal(moves);
		bool check = InCheck(side);
		if (moves.empty())
			return check ? STATUS_CHECKMATE : STATUS_STALEMATE;
		if (halfmove >= 100)
			return STATUS_DRAW_FIFTY;
		if (InsufficientMaterial())
			return STATUS_DRAW_MATERIAL;
		return check ? STATUS_CHECK : STATUS_PLAYING;
	}

	// K v K, K+minor v K:
	bool InsufficientMaterial() const
	{
		int minors = 0;
		for (int sq = 0; sq < 64; sq++)
		{
			int t = TypeOf(board[sq]);
			if (t == PAWN || t == ROOK || t == QUEEN)
				return false;
			if (t == KNIGHT || t == BISHOP)
				minors++;
		}
		return minors <= 1;
	}

	// where the rook goes when the king castles with move m:
	static void CastleRookSquares(const Move& m, int& rookFrom, int& rookTo)
	{
		if (m.to > m.from)	{ rookFrom = m.from + 3; rookTo = m.from + 1; }	// king side
		else			{ rookFrom = m.from - 4; rookTo = m.from - 1; }	// queen side
	}

private:
	struct Undo
	{
		Move move;
		int8_t captured;
		int castling;
		int epSquare;
		int halfmove;
	};
	std::vector<Undo> history;

	// first 4 are straight, last 4 are diagonal -- IsAttacked and AddSlides rely on that order
	static constexpr int KingDF[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static constexpr int KingDR[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	static constexpr int KnightDF[8] = { 1, 2, 2, 1, -1, -2, -2, -1 };
	static constexpr int KnightDR[8] = { 2, 1, -1, -2, -2, -1, 1, 2 };

	static int TypeFromChar(char c)
	{
		switch (c | 0x20)	// lower case
		{
		case 'p': return PAWN;
		case 'n': return KNIGHT;
		case 'b': return BISHOP;
		case 'r': return ROOK;
		case 'q': return QUEEN;
		case 'k': return KING;
		}
		return NO_PIECE;
	}

	// moving from or to one of these squares loses the matching castling right:
	static int CastleMask(int sq)
	{
		switch (sq)
		{
		case 0:  return ~CASTLE_WQ;			// a1
		case 4:  return ~(CASTLE_WK | CASTLE_WQ);	// e1
		case 7:  return ~CASTLE_WK;			// h1
		case 56: return ~CASTLE_BQ;			// a8
		case 60: return ~(CASTLE_BK | CASTLE_BQ);	// e8
		case 63: return ~CASTLE_BK;			// h8
		}
		return ~0;
	}

	static void AddMove(std::vector<Move>& moves, int from, int to, int flags, int promo = NO_PIECE)
	{
		Move m;
		m.from = (int8_t)from;
		m.to = (int8_t)to;
		m.flags = (uint8_t)flags;
		m.promo = (int8_t)promo;
		moves.push_back(m);
	}

	static void AddPawnMove(std::vector<Move>& moves, int from, int to, int flags, bool promotes)
	{
		if (!promotes)
		{
			AddMove(moves, from, to, flags);
			return;
		}
		for (int t = QUEEN; t >= KNIGHT; t--)
			AddMove(moves, from, to, flags | MF_PROMOTION, t);
	}

	void AddStep(std::vector<Move>& moves, int from, int nf, int nr) const
	{
		if (!OnBoard(nf, nr))
			return;
		int to = MakeSquare(nf, nr);
		if (board[to] == 0)
			AddMove(moves, from, to, 0);
		else if (SideOf(board[to]) != side)
			AddMove(moves, from, to, MF_CAPTURE);
	}

	void AddSlides(std::vector<Move>& moves, int from, int firstDir, int lastDir) const
	{
		for (int d = firstDir; d < lastDir; d++)
		{
			int nf = FileOf(from) + KingDF[d], nr = RankOf(from) + KingDR[d];
			while (OnBoard(nf, nr))
			{
				int to = MakeSquare(nf, nr);
				if (board[to] == 0)
					AddMove(moves, from, to, 0);
				else
				{
					if (SideOf(board[to]) != side)
						AddMove(moves, from, to, MF_CAPTURE);
					break;
				}
				nf += KingDF[d];
				nr += KingDR[d];
			}
		}
	}

	void AddCastles(std::vector<Move>& moves, int kingSq) const
	{
		int home = (side == SIDE_WHITE) ? 4 : 60;
		if (kingSq != home)
			return;
		int enemy = side ^ 1;
		int kRight = (side == SIDE_WHITE) ? CASTLE_WK : CASTLE_BK;
		int qRight = (side == SIDE_WHITE) ? CASTLE_WQ : CASTLE_BQ;

		if ((castling & kRight) && board[home + 1] == 0 && board[home + 2] == 0 &&
			!IsAttacked(home, enemy) && !IsAttacked(home + 1, enemy) && !IsAttacked(home + 2, enemy))
			AddMove(moves, home, home + 2, MF_CASTLE);

		if ((castling & qRight) && board[home - 1] == 0 && board[home - 2] == 0 && board[home - 3] == 0 &&
			!IsAttacked(home, enemy) && !IsAttacked(home - 1, enemy) && !IsAttacked(home - 2, enemy))
			AddMove(moves, home, home - 2, MF_CASTLE);
	}
};

#endif	// CHESSRULES_H
