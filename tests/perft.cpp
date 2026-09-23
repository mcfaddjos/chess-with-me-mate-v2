// perft.cpp -- counts every legal move sequence to a fixed depth and compares
// against published totals (https://www.chessprogramming.org/Perft_Results).
// If move generation is wrong anywhere (castling, en passant, promotion, pins,
// checks) these numbers drift, so a pass here is strong evidence the rules hold.

#include <cstdio>
#include "../chessrules.h"

static long long Perft(ChessRules& game, int depth)
{
	std::vector<Move> moves;
	game.GenerateLegal(moves);
	if (depth == 1)
		return (long long)moves.size();

	long long nodes = 0;
	for (const Move& m : moves)
	{
		game.MakeMove(m);
		nodes += Perft(game, depth - 1);
		game.UnmakeMove();
	}
	return nodes;
}

struct PerftCase
{
	const char* name;
	const char* fen;
	int depth;
	long long expected;
};

int main()
{
	const PerftCase cases[] =
	{
		{ "start",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, 20 },
		{ "start",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, 8902 },
		{ "start",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281 },
		{ "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, 48 },
		{ "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862 },
		{ "pos3",     "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238 },
		{ "pos4",     "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467 },
		{ "pos5",     "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379 },
	};

	int failures = 0;
	for (const PerftCase& c : cases)
	{
		ChessRules game;
		game.LoadFEN(c.fen);
		long long got = Perft(game, c.depth);
		bool ok = (got == c.expected);
		if (!ok)
			failures++;
		printf("%-4s %-9s depth %d: %9lld (expected %9lld)\n", ok ? "ok" : "FAIL", c.name, c.depth, got, c.expected);
	}

	// status checks:
	ChessRules game;
	game.LoadFEN("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");	// fool's mate
	bool mateOk = (game.Status() == STATUS_CHECKMATE);
	game.LoadFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
	bool staleOk = (game.Status() == STATUS_STALEMATE);
	game.LoadFEN("8/8/4k3/8/8/3NK3/8/8 w - - 0 1");
	bool materialOk = (game.Status() == STATUS_DRAW_MATERIAL);
	printf("%-4s checkmate detection\n", mateOk ? "ok" : "FAIL");
	printf("%-4s stalemate detection\n", staleOk ? "ok" : "FAIL");
	printf("%-4s insufficient material\n", materialOk ? "ok" : "FAIL");
	failures += !mateOk + !staleOk + !materialOk;

	printf(failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
	return failures == 0 ? 0 : 1;
}
