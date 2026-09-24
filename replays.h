// replays.h -- games you can watch move by move (right-click > Watch a Game).
//
// Each move is written from-square/to-square (UCI style: "e2e4", promotions add the piece:
// "f2g1n"), optionally followed by '!' (a strong move, shown with a green arrow) or '?' (a
// mistake, red arrow). Every line is checked move by move against the rules in
// tests/engine_tests.cpp, so a typo here fails the tests instead of showing a wrong game.
//
// To add a game: append a ReplayGame to the list in Replays().

#ifndef REPLAYS_H
#define REPLAYS_H

#include <string>
#include <vector>
#include "chessrules.h"

struct ReplayPly
{
	const char* move;		// "e2e4", "e2e3?", "f2g1n!"
	const char* comment;	// shown while this move is the last one played
};

struct ReplayGame
{
	const char* title;
	const char* subtitle;
	const char* fen;		// nullptr = the normal starting position
	const char* intro;		// shown before the first move
	const char* result;		// shown after the last move
	std::vector<ReplayPly> plies;
};

inline const std::vector<ReplayGame>& Replays()
{
	static const std::vector<ReplayGame> games =
	{
		{
			"LASKER TRAP", "ALBIN COUNTERGAMBIT - BLACK WINS", nullptr,
			"A FAMOUS TRAP AFTER 1.d4 d5. WATCH WHITE'S e3 GO WRONG.",
			"BLACK IS WINNING: WHITE'S KING IS STUCK IN THE CENTER.",
			{
				{ "d2d4",   "WHITE CLAIMS THE CENTER." },
				{ "d7d5",   "BLACK MATCHES IT." },
				{ "c2c4",   "QUEEN'S GAMBIT: WHITE OFFERS THE C-PAWN." },
				{ "e7e5!",  "ALBIN COUNTERGAMBIT: BLACK GIVES A PAWN TO ATTACK." },
				{ "d4e5",   "WHITE TAKES IT." },
				{ "d5d4",   "THE D-PAWN DIGS IN AND CRAMPS WHITE." },
				{ "e2e3?",  "MISTAKE: THIS OPENS LINES TO WHITE'S KING. Nf3 IS RIGHT." },
				{ "f8b4!",  "CHECK - THE BISHOP USES THE NEWLY OPEN DIAGONAL." },
				{ "c1d2",   "WHITE BLOCKS WITH THE BISHOP." },
				{ "d4e3!",  "THE PAWN TAKES AND NOW HITS f2. (fxe3 WAS WHITE'S BEST)" },
				{ "d2b4?",  "BLUNDER: WHITE GRABS THE BISHOP AND WALKS INTO THE TRAP." },
				{ "e3f2",   "CHECK! Kxf2 WOULD LOSE THE QUEEN TO ...Qxd1." },
				{ "e1e2",   "SO THE KING HAS TO STEP UP." },
				{ "f2g1n!", "PROMOTE TO A KNIGHT, WITH CHECK! A QUEEN WOULD BE TRADED." },
				{ "h1g1",   "THE ROOK TAKES THE NEW KNIGHT." },
				{ "c8g4!",  "SKEWER: CHECK, AND WHITE'S QUEEN IS BEHIND THE KING." },
				{ "e2e1",   "THE KING STEPS BACK. THE QUEEN IS STILL ATTACKED." },
				{ "d8h4",   "CHECK AGAIN - BLACK KEEPS THE KING ON THE RUN." },
				{ "e1d2",   "THE KING RUNS FOR THE CENTER." },
				{ "b8c6",   "EVERY BLACK PIECE JOINS THE ATTACK." },
			}
		},
		{
			"YOUR OPENING: THE LOOSE BISHOP", "1.d4 d5 2.e3 - WHITE WINS A PIECE", nullptr,
			"FROM YOUR GAME: ALWAYS ASK 'IS ANYTHING OF THEIRS UNPROTECTED?'",
			"WHITE IS A WHOLE BISHOP UP.",
			{
				{ "d2d4",   "QUEEN'S PAWN GAME." },
				{ "d7d5",   "BLACK CLAIMS THE CENTER TOO." },
				{ "e2e3",   "A QUIET MOVE - IT ALSO OPENS THE d1-h5 DIAGONAL FOR THE QUEEN." },
				{ "c8g4?",  "MISTAKE: NOTHING PROTECTS THIS BISHOP." },
				{ "d1g4!",  "THE QUEEN SEES g4 ALONG d1-e2-f3-g4 AND TAKES IT FOR FREE." },
			}
		},
		{
			"FOOL'S MATE", "THE FASTEST CHECKMATE - BLACK WINS", nullptr,
			"TWO WEAK PAWN MOVES OPEN WHITE'S KING TO THE QUEEN.",
			"CHECKMATE IN TWO MOVES.",
			{
				{ "f2f3?",  "WEAKENS THE KING'S DIAGONAL (e1-h4)." },
				{ "e7e5",   "BLACK OPENS A PATH FOR THE QUEEN." },
				{ "g2g4?",  "BLUNDER: THE DIAGONAL IS NOW WIDE OPEN." },
				{ "d8h4!",  "CHECKMATE - NOTHING CAN BLOCK OR TAKE THE QUEEN." },
			}
		},
		{
			"SCHOLAR'S MATE", "A CLASSIC BEGINNER TRAP - WHITE WINS", nullptr,
			"QUEEN AND BISHOP BOTH AIM AT f7, BLACK'S WEAKEST SQUARE.",
			"CHECKMATE. DEFEND f7 WITH ...g6 OR ...Qe7 INSTEAD OF ...Nf6.",
			{
				{ "e2e4",   "WHITE OPENS LINES FOR THE QUEEN AND BISHOP." },
				{ "e7e5",   "A NORMAL REPLY." },
				{ "f1c4",   "THE BISHOP AIMS AT f7." },
				{ "b8c6",   "BLACK DEVELOPS." },
				{ "d1h5",   "THE QUEEN AIMS AT f7 TOO: TWO ATTACKERS, ONE DEFENDER." },
				{ "g8f6?",  "BLUNDER: IT ATTACKS THE QUEEN BUT IGNORES THE THREAT." },
				{ "h5f7!",  "CHECKMATE - THE BISHOP GUARDS THE QUEEN ON f7." },
			}
		},
	};
	return games;
}

// turns "e2e4", "e2e3?" or "f2g1n!" into the legal move it names in 'pos'; mark gets '!', '?' or 0
inline bool ParseReplayMove(ChessRules& pos, const std::string& text, Move& out, char& mark)
{
	mark = 0;
	std::string t = text;
	if (!t.empty() && (t.back() == '!' || t.back() == '?'))
	{
		mark = t.back();
		t.pop_back();
	}
	if (t.size() < 4)
		return false;
	int promo = NO_PIECE;
	if (t.size() == 5)
	{
		switch (t[4])
		{
		case 'q': promo = QUEEN; break;
		case 'r': promo = ROOK; break;
		case 'b': promo = BISHOP; break;
		case 'n': promo = KNIGHT; break;
		default: return false;
		}
	}
	std::vector<Move> legal;
	pos.GenerateLegal(legal);
	for (const Move& m : legal)
	{
		if (SquareName(m.from) == t.substr(0, 2) && SquareName(m.to) == t.substr(2, 2) &&
			(promo == NO_PIECE ? !(m.flags & MF_PROMOTION) : m.promo == promo))
		{
			out = m;
			return true;
		}
	}
	return false;
}

#endif	// REPLAYS_H
