// engine_tests.cpp -- checks the computer opponent and the adaptive rating math.
// Every bot move uses a fixed seed, so results are repeatable.

#include <cstdio>
#include "../engine.h"
#include "../replays.h"

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

// plays "e2e4 e7e5 ..." from a position, returning the moves (as the game would record them)
static std::vector<Move> PlayLine(ChessRules pos, const char* line)
{
	std::vector<Move> played;
	std::string s(line);
	for (size_t i = 0; i + 4 <= s.size(); i += 5)
	{
		std::vector<Move> legal;
		pos.GenerateLegal(legal);
		for (const Move& m : legal)
			if (SquareName(m.from) == s.substr(i, 2) && SquareName(m.to) == s.substr(i + 2, 2) &&
				(!(m.flags & MF_PROMOTION) || m.promo == QUEEN))
			{
				played.push_back(m);
				pos.MakeMove(m);
				break;
			}
	}
	return played;
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
	Check(BotRatingFor(1000, true) == 1250, "a challenge bot is rated well above the player");

	// ---------------- post-game analysis ----------------
	Check(std::fabs(WinChance(0) - 50.0) < 0.01 && WinChance(300) > 70.0 && WinChance(-300) < 30.0,
		"win chance: even is 50%, +3 pawns is winning");
	Check(MoveAccuracy(60, 60) > 99.9 && MoveAccuracy(60, 20) < 20.0, "move accuracy: no loss ~100, big loss low");

	{
		ChessRules pos;
		pos.Reset();
		std::vector<Move> legal;
		pos.GenerateLegal(legal);
		std::string knight;
		for (const Move& m : legal)
			if (IsMove(m, "b1", "c3"))
				knight = pos.MoveName(m);
		pos.LoadFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
		pos.GenerateLegal(legal);
		std::string castle;
		for (const Move& m : legal)
			if (IsMove(m, "e1", "g1"))
				castle = pos.MoveName(m);
		Check(knight == "Nb1-c3" && castle == "O-O", "move names: Nb1-c3, O-O");
	}

	// fool's mate: after 1.f3 e5, white's g2-g4?? allows Qd8-h4# (a real mate in one -- nothing can block)
	{
		ChessRules start;
		start.LoadFEN("rnbqkbnr/pppp1ppp/8/4p3/8/5P2/PPPPP1PP/RNBQKBNR w KQkq - 0 2");
		std::vector<Move> game = PlayLine(start, "g2g4 d8h4");
		ChessRules end = start;
		for (const Move& m : game)
			end.MakeMove(m);
		Check(game.size() == 2 && end.Status() == STATUS_CHECKMATE, "test setup: g4?? Qh4 really is checkmate");
		std::atomic<bool> stop(false);
		std::atomic<int> progress(0);
		GameReport r = AnalyzeGame(start, game, SIDE_WHITE, stop, progress);
		bool ok = r.valid && r.moves.size() == 1 && r.moves[0].verdict == VERDICT_BLUNDER &&
			r.moves[0].replyName == "Qd8-h4" && r.moves[0].bestName != "g2-g4" && r.lessons.size() == 1;
		Check(ok, "analysis: g2-g4 is a BLUNDER that allowed Qd8-h4");
		Check(r.valid && !r.moves.empty() && LessonCause(r.moves[0]) == "IT ALLOWED Qd8-h4",
			"lesson wording: a move that hands over mate says 'IT ALLOWED Qd8-h4'");

		// the same analysis on a "machine too busy to search at all": zero seconds per move.
		// The guaranteed minimum depth must still see the mate (it failed in-game under load).
		GameReport busy = AnalyzeGame(start, game, SIDE_WHITE, stop, progress, 0.0);
		Check(busy.valid && busy.moves.size() == 1 && busy.moves[0].verdict == VERDICT_BLUNDER && busy.moves[0].depth >= 3,
			"analysis with no time budget still finds the blunder (minimum depth 3)");
		if (r.valid && !r.moves.empty())
			printf("       (played %s, better %s, allowed %s, accuracy %.0f%%)\n", r.moves[0].playedName.c_str(),
				r.moves[0].bestName.c_str(), r.moves[0].replyName.c_str(), r.accuracy);
	}

	// scholar's mate: black's ...Nf6 walks into Qxf7#
	{
		ChessRules start;
		start.Reset();
		std::vector<Move> game = PlayLine(start, "e2e4 e7e5 f1c4 b8c6 d1h5 g8f6 h5f7");
		std::atomic<bool> stop(false);
		std::atomic<int> progress(0);
		GameReport black = AnalyzeGame(start, game, SIDE_BLACK, stop, progress);
		bool flagged = false;
		for (int i : black.lessons)
			if (black.moves[i].playedName == "Ng8-f6" && black.moves[i].verdict == VERDICT_BLUNDER)
				flagged = true;
		Check(black.valid && flagged, "analysis: black's ...Ng8-f6 is flagged as a BLUNDER");
		GameReport white = AnalyzeGame(start, game, SIDE_WHITE, stop, progress);
		Check(white.valid && white.accuracy > black.accuracy, "analysis: the winner's accuracy beats the loser's");
		printf("       (white %.0f%%, black %.0f%%)\n", white.accuracy, black.accuracy);
	}

	// the game from the screenshot: 1.d4 d5 2.e3 Bg4? 3.Be2 -- white missed 3.Qxg4 winning a bishop
	{
		ChessRules start;
		start.Reset();
		std::vector<Move> game = PlayLine(start, "d2d4 d7d5 e2e3 c8g4 f1e2");
		std::atomic<bool> stop(false);
		std::atomic<int> progress(0);
		GameReport r = AnalyzeGame(start, game, SIDE_WHITE, stop, progress);
		std::string cause;
		for (const MoveReview& m : r.moves)
			if (m.playedName == "Bf1-e2")
				cause = LessonCause(m);
		Check(cause == "YOU MISSED A CHANCE TO WIN A BISHOP", "lesson wording: 3.Be2 says 'YOU MISSED A CHANCE TO WIN A BISHOP'");
		if (cause != "YOU MISSED A CHANCE TO WIN A BISHOP")
			printf("       (got '%s')\n", cause.c_str());
	}

	// ---------------- replays: every stored game must be legal, move by move ----------------
	for (const ReplayGame& g : Replays())
	{
		ChessRules pos;
		if (g.fen != nullptr)
			pos.LoadFEN(g.fen);
		bool legal = true;
		for (const ReplayPly& p : g.plies)
		{
			Move m;
			char mark;
			if (!ParseReplayMove(pos, p.move, m, mark))
			{
				printf("       illegal or unreadable move '%s'\n", p.move);
				legal = false;
				break;
			}
			pos.MakeMove(m);
		}
		std::string title = g.title;
		bool mateExpected = title == "FOOL'S MATE" || title == "SCHOLAR'S MATE";
		bool ok = legal && (!mateExpected || pos.Status() == STATUS_CHECKMATE);
		Check(ok, (std::string("replay is a legal game: ") + title).c_str());
	}
	Check(std::string(RatingClass(1050)) == "CLASS E" && std::string(RatingClass(2250)) == "MASTER", "US Chess class labels");

	printf(failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
	return failures == 0 ? 0 : 1;
}
