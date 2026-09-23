// engine_tests.cpp -- checks the computer opponent and the adaptive rating math.
// Every bot move uses a fixed seed, so results are repeatable.

#include <cstdio>
#include "../engine.h"

static int failures = 0;

static void Check(bool ok, const char* what)
{
	printf("%-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

static Move BotMove(const char* fen, int elo, unsigned seed = 1)
{
	ChessRules pos;
	pos.LoadFEN(fen);
	Engine engine;
	std::atomic<bool> stop(false);
	return engine.Choose(pos, LevelForElo(elo), stop, seed);
}

static bool IsMove(const Move& m, const char* from, const char* to)
{
	return SquareName(m.from) == from && SquareName(m.to) == to;
}

int main()
{
	// back-rank mate in one: Ra1-a8#
	const char* backRank = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1";
	Check(IsMove(BotMove(backRank, 1600), "a1", "a8"), "1600 bot finds a back-rank mate in one");
	Check(IsMove(BotMove(backRank, 2200), "a1", "a8"), "2200 bot finds a back-rank mate in one");

	// a hanging queen on d5, free for the e4 pawn:
	const char* freeQueen = "rnb1kbnr/pppp1ppp/8/3q4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 3";
	int took = 0;
	for (unsigned seed = 1; seed <= 20; seed++)
		if (IsMove(BotMove(freeQueen, 1800, seed), "e4", "d5"))
			took++;
	Check(took == 20, "1800 bot always takes a free queen (20 seeds)");

	// the bot must not blunder into a mate in one when a strong level looks 2+ plies ahead:
	// black threatens Qxf2# (queen h4 + bishop c5 on f2); white must defend
	const char* threat = "r1b1k1nr/pppp1ppp/2n5/2b1p3/2B1P2q/2N5/PPPP1PPP/R1BQK1NR w KQkq - 4 5";
	{
		// sanity check the setup: ignoring the threat (a2-a3) really does allow mate
		ChessRules pos;
		pos.LoadFEN(threat);
		Move a3;
		a3.from = 8;	// a2
		a3.to = 16;	// a3
		pos.MakeMove(a3);
		std::vector<Move> replies;
		pos.GenerateLegal(replies);
		bool mateExists = false;
		for (const Move& r : replies)
		{
			pos.MakeMove(r);
			mateExists = mateExists || pos.Status() == STATUS_CHECKMATE;
			pos.UnmakeMove();
		}
		Check(mateExists, "test setup: the threat position really threatens mate");
	}
	{
		ChessRules pos;
		pos.LoadFEN(threat);
		Move m = BotMove(threat, 2000);
		pos.MakeMove(m);
		std::vector<Move> replies;
		pos.GenerateLegal(replies);
		bool allowsMate = false;
		for (const Move& r : replies)
		{
			pos.MakeMove(r);
			if (pos.Status() == STATUS_CHECKMATE)
				allowsMate = true;
			pos.UnmakeMove();
		}
		Check(!allowsMate, "2000 bot does not allow a mate in one");
	}

	// every level returns a legal move from a busy middlegame:
	const char* middle = "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 8";
	bool allLegal = true;
	for (int elo = 200; elo <= 2400; elo += 200)
	{
		ChessRules pos;
		pos.LoadFEN(middle);
		std::vector<Move> legal;
		pos.GenerateLegal(legal);
		Move m = BotMove(middle, elo);
		bool found = false;
		for (const Move& l : legal)
			if (l.from == m.from && l.to == m.to && l.promo == m.promo)
				found = true;
		allLegal = allLegal && found;
	}
	Check(allLegal, "every level (200-2400) returns a legal move");

	// strength settings never get weaker as the rating goes up:
	bool monotonic = true;
	for (int elo = RATING_MIN; elo < RATING_MAX; elo += 50)
	{
		BotLevel a = LevelForElo(elo), b = LevelForElo(elo + 50);
		if (b.maxDepth < a.maxDepth || b.temperature > a.temperature || b.thinkSeconds < a.thinkSeconds)
			monotonic = false;
	}
	Check(monotonic, "depth/temperature/think time are monotonic in rating");

	// rating math:
	Check(AdjustRating(1000, 1000, 1.0, 0) == 1020, "win vs equal opponent: +20 (K=40)");
	Check(AdjustRating(1000, 1000, 0.0, 0) == 980, "loss vs equal opponent: -20");
	Check(AdjustRating(1000, 1000, 0.5, 0) == 1000, "draw vs equal opponent: no change");
	Check(AdjustRating(1000, 1400, 1.0, 30) > AdjustRating(1000, 1000, 1.0, 30), "beating a stronger bot gains more");
	Check(AdjustRating(RATING_MIN, 1000, 0.0, 0) == RATING_MIN, "rating never drops below the floor");
	Check(BotRatingFor(1000) == 1050, "next bot is rated a little above the player");
	Check(std::string(RatingClass(1050)) == "CLASS E" && std::string(RatingClass(2250)) == "MASTER", "US Chess class labels");

	printf(failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
	return failures == 0 ? 0 : 1;
}
