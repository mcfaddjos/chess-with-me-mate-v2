#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <algorithm>

#define _USE_MATH_DEFINES
#include <math.h>

#ifndef F_PI
#define F_PI		((float)(M_PI))
#define F_2_PI		((float)(2.f*F_PI))
#define F_PI_2		((float)(F_PI/2.f))
#endif


#ifdef WIN32
#include <windows.h>
#pragma warning(disable:4996)
#endif

#include "glew.h"
#include <GL/gl.h>
#include <GL/glu.h>
#include "glut.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "chessrules.h"
#include "engine.h"
#include <thread>
#include <atomic>
#include <fstream>
#ifdef WIN32
#include <direct.h>		// _mkdir
#endif

// CS 450 / 550 --Fall Quarter 2023
// Final Project -- Chess With Me Mate?
// v2: real chess rules, orbit camera, time-based animation, redraw-on-demand

// Author:			Joseph McFadden

#pragma region FunctionSet

// function prototypes:

void	Animate();
void	Display();
void	DoAxesMenu(int);
void	DoColorMenu(int);
void	DoDepthMenu(int);
void	DoDebugMenu(int);
void	DoMainMenu(int);
void	DoProjectMenu(int);
void	DoViewMenu(int);
void	DoRasterString(float, float, float, char*);
void	InitGraphics();
void	InitLists();
void	InitMenus();
void	Keyboard(unsigned char, int, int);
void	SpecialKeys(int, int, int);
void	MouseButton(int, int, int, int);
void	MouseMotion(int, int);
void	MousePassive(int, int);
void	Reset();
void	Resize(int, int);
void	Visibility(int);
void	SyncPiecesToBoard();
void	RefreshGameState();
void	LoadProfile();
void	StartNewGame(const char*);
void	StartBot();
void	StopBot();
void	RecordResultIfOver();
void	SetView(int);

void	Axes(float);
void	Cross(float[3], float[3], float[3]);
float	Unit(float[3], float[3]);
float*	Array3(float, float, float);
float*	MulArray3(float, float, float, float);
float*	MulArray3(float, float[]);

#pragma endregion

#pragma region Colors

// window background color (rgba):

const GLfloat BLACK[] = { 0., 0., 0., 1. };
const GLfloat GRAY[] = { 0.5f, 0.5f, 0.5f, 1.0f };
const float	WHITE[] = { 1.,1.,1.,1. };


// the color numbers:
// this order must match the radio button order, which must match the order of the color names,
// 	which must match the order of the color RGB values

char* ColorNames[] =
{
	(char*)"Red",
	(char*)"Yellow",
	(char*)"Green",
	(char*)"Cyan",
	(char*)"Blue",
	(char*)"Magenta",
	(char*)"White",
};

// the color definitions:
// this order must match the menu order

const GLfloat Colors[][3] =
{
	{ 1., 0., 0. },		// red
	{ 1., 1., 0. },		// yellow
	{ 0., 1., 0. },		// green
	{ 0., 1., 1. },		// cyan
	{ 0., 0., 1. },		// blue
	{ 1., 0., 1. },		// magenta
	{ 1.,1.,1. }		// white
};

enum ColorIndex
{
	RED,
	YELLOW,
	GREEN,
	CYAN,
	BLUE,
	MAGENTA,
	LIGHT_WHITE
};

#pragma endregion

#pragma region IncludedFiles

#include "setmaterial.cpp"
#include "setlight.cpp"
#include "loadobjfile.cpp"
#include "keytime.cpp"

#pragma endregion

#pragma region Constant Global Variables

// title of the window:
const char* WINDOWTITLE = "Chess With Me Mate? - Joseph McFadden";

// the escape key:
const int ESCAPE = 0x1b;

// initial window size:
const int viewportHeight = 700;
const int viewportWidth = 700;

// scroll wheel button values:
const int SCROLL_WHEEL_UP = 3;
const int SCROLL_WHEEL_DOWN = 4;

// line width for the axes:
const GLfloat AXES_WIDTH = 3.;

// the board: each tile is TILE x TILE world units, centered on the origin
const float TILE = 20.f;
const float HIGHLIGHT_Y = 0.2f;		// highlights float just above the board to avoid z-fighting
const float SELECTED_LIFT = 4.f;	// selected piece sits up off its square
const float BORDER = 16.f;			// width of the frame around the board, holds the a-h / 1-8 labels

// camera (orbits the center of the board):
const float CAM_FOV = 20.f;		// degrees
const float CAM_DIST = 620.f;		// far enough to fit the board and its border
const float CAM_DIST_MIN = 250.f;
const float CAM_DIST_MAX = 1100.f;
const float CAM_PITCH = 68.f;		// degrees above the board
const float CAM_PITCH_MIN = 12.f;
const float CAM_PITCH_MAX = 89.5f;
const float ORBIT_SPEED = 0.4f;		// degrees per pixel dragged
const float ORBIT_KEY_STEP = 5.f;	// degrees per arrow key press
const float ZOOM_FACTOR = 1.1f;

// animation timing, in seconds:
const float MOVE_TIME_BASE = 0.16f;
const float MOVE_TIME_PER_TILE = 0.05f;
const float MOVE_TIME_MAX = 0.55f;
const float HOP_HEIGHT = 5.f;
const float KNIGHT_HOP_HEIGHT = 16.f;
const float CONTACT_FRACTION = 0.8f;	// how far into a capture the victim starts to fall
const float FALL_TIME = 0.9f;
const float FADE_TIME = 0.45f;

// lighting:
const float LIGHT_RADIUS = 20.0f;
const float LIGHT_HEIGHT = 90.0f;

#pragma endregion

#pragma region Enums

// which projection:
enum Projections
{
	ORTHO,
	PERSP
};

// main menu entries:
enum ButtonVals
{
	RESET,
	QUIT,
	NEW_GAME,
	UNDO,
	RESIGN
};

// who you play against, and which color you get:
enum Opponents
{
	OPPONENT_COMPUTER,
	OPPONENT_HUMAN
};

enum ColorChoices
{
	COLOR_ALTERNATE,
	COLOR_WHITE,
	COLOR_BLACK
};

// camera presets:
enum Views
{
	VIEW_WHITE,
	VIEW_BLACK,
	VIEW_TOP
};

std::string PieceTypeToString(int type)
{
	switch (type) {
	case PAWN:   return "Pawn";
	case KNIGHT: return "Knight";
	case BISHOP: return "Bishop";
	case ROOK:   return "Rook";
	case QUEEN:  return "Queen";
	case KING:   return "King";
	default:     return "Unknown";
	}
}

#pragma endregion

#pragma region Global Classes

// One of the 32 pieces on screen. The rules live in ChessRules; this is just
// where a piece is drawn and what it is doing (sliding, falling, fading).
struct Piece
{
	int type = PAWN;
	bool isWhite = true;
	int square = -1;		// board square, or -1 once captured
	bool visible = false;
	glm::vec3 pos = glm::vec3(0.f);

	// sliding/hopping to a new square:
	bool moving = false;
	glm::vec3 moveFrom = glm::vec3(0.f);
	glm::vec3 moveTo = glm::vec3(0.f);
	float moveStart = 0.f;
	float moveTime = 0.f;
	float hop = 0.f;
	int promoteTo = NO_PIECE;
	Piece* victim = nullptr;

	// tipping over and fading out after being captured:
	bool dying = false;
	float deathStart = 0.f;
	glm::vec3 fallAxis = glm::vec3(1.f, 0.f, 0.f);
	float opacity = 1.f;

	bool Busy() const { return moving || dying; }
};

#pragma endregion

#pragma region Non-Constant Global Variables

int		ActiveButton;			// current button that is down
GLuint	AxesList, BoardList;
GLuint	PieceLists[KING + 1];		// one display list per piece type, shared by both colors
glm::vec3 PieceMin[KING + 1], PieceMax[KING + 1];	// model extents, a quick first test for picking
std::vector<glm::vec3> PieceTris[KING + 1];			// model triangles (3 verts each), for exact picking
int		AxesOn;					// != 0 means to draw the axes
int		DebugOn;				// != 0 means to draw the pick ray and bounding boxes
int		DepthCueOn;				// != 0 means to use intensity depth cueing
bool	Frozen;
int		MainWindow;				// window id for main graphics window
int		NowColor;				// index into Colors[ ]
int		NowProjection;			// ORTHO or PERSP
int		Xmouse, Ymouse;			// mouse values
int		LightColorNow = LIGHT_WHITE;
bool	isSpotLight = false;	// default is a point light

// camera:
float	CamYaw, CamPitch, CamDist;	// degrees, degrees, world units
glm::vec3 CamEye;
glm::mat4 ViewMatrix;
glm::mat4 ProjMatrix;
glm::vec4 Viewport;				// x, y, width, height of the square drawing area

// animation clock -- only advances while something is animating and we're not frozen:
float	AnimTime;
int		LastAnimMs;
bool	Animating;
Keytimes FallingAnimation;

// the game:
ChessRules Rules;
Piece	Pieces[32];
Piece*	PieceAt[64];			// which on-screen piece is standing on each square
std::vector<Move> LegalMoves;	// every legal move for the side to move
GameStatus Status;
int		SelectedSq = -1;
int		HoverSq = -1;
std::string LastMoveText;

// game options (right-click menu > Game Options):
struct TimeControl
{
	const char* name;
	int minutes;		// 0 = untimed
	int increment;		// seconds added after each move
};
const TimeControl TimeControls[] =
{
	{ "Untimed",         0,  0 },
	{ "Bullet 1+0",      1,  0 },
	{ "Blitz 3+2",       3,  2 },
	{ "Blitz 5+0",       5,  0 },
	{ "Rapid 10+0",     10,  0 },
	{ "Rapid 15+10",    15, 10 },
	{ "Classical 30+0", 30,  0 },
};
const int NUM_TIME_CONTROLS = sizeof(TimeControls) / sizeof(TimeControls[0]);
int		TimeControlNow = 0;
bool	ShowHints = true;
bool	AutoFlip = false;
bool	ChoosePromotion = false;
int		Opponent = OPPONENT_COMPUTER;
int		ColorChoice = COLOR_ALTERNATE;
int		TimeMenu, HintsMenu, FlipMenu, PromoMenu, OpponentMenu, ColorMenu;	// glut menu ids, relabeled to mark the current choice
int		StartClockSeconds = 0;	// -clock N (testing): overrides the time control's starting time

// the chess clock:
int		ClockMs[2];			// time left, indexed by SIDE_WHITE / SIDE_BLACK
bool	ClockRunning;		// true from white's first move until the game ends
bool	ClockPaused;
int		ClockLastMs;
int		FlagSide = -1;		// side that ran out of time, or -1
std::string ClockShown;		// last clock text drawn, to redraw only when it changes

// a pawn reached the last rank and we're waiting for 1-4 to pick the piece:
int		PendingPromoFrom = -1, PendingPromoTo = -1;

// the computer opponent -- searches on a worker thread so the window stays responsive:
struct Profile
{
	int rating = RATING_START;
	int games = 0, wins = 0, losses = 0, draws = 0;
};
Profile	Player;
std::string ProfilePath;		// where Player is saved between sessions
int		HumanSide = SIDE_WHITE;	// in a computer game, which side you play
int		BotElo = RATING_START;	// this game's bot rating
int		FixedBotElo = 0;		// -botelo N (testing): pin the bot's rating instead of adapting
unsigned BotSeed = 0;			// -seed N (testing): repeatable bot moves; 0 = random
bool	ResultRecorded;			// this game's result already went into Player
int		ResignedSide = -1;		// side that resigned, or -1
std::string RatingNote;			// e.g. "RATING 1000 -> 1016 (+16)" after a game

std::thread BotThread;
std::atomic<bool> BotStop(false);
std::atomic<bool> BotDone(false);
Move	BotResult;
bool	BotThinking;
int		BotGeneration;			// bumped whenever a search is abandoned, so stale results are ignored
int		BotStartMs;

// last pick ray, drawn in debug mode:
glm::vec3 RayOrigin, RayDir;
bool	HaveRay;

#pragma endregion

#pragma region CustomMathClasses
// utility to create an array from 3 separate values:

float *
Array3( float a, float b, float c )
{
	static float array[4];

	array[0] = a;
	array[1] = b;
	array[2] = c;
	array[3] = 1.;
	return array;
}

// utility to create an array from a multiplier and an array:

float *
MulArray3( float factor, float array0[ ] )
{
	static float array[4];

	array[0] = factor * array0[0];
	array[1] = factor * array0[1];
	array[2] = factor * array0[2];
	array[3] = 1.;
	return array;
}


float *
MulArray3(float factor, float a, float b, float c )
{
	static float array[4];

	float* abc = Array3(a, b, c);
	array[0] = factor * abc[0];
	array[1] = factor * abc[1];
	array[2] = factor * abc[2];
	array[3] = 1.;
	return array;
}
#pragma endregion

#pragma region Main
// main program:

int
main( int argc, char *argv[ ] )
{
	// turn on the glut package:
	// (do this before checking argc and argv since glutInit might
	// pull some command line arguments out)

	glutInit( &argc, argv );

	// setup all the graphics stuff:

	InitGraphics( );

	// init all the global variables used by Display( ):

	Reset( );

	// command line (mostly for testing):
	//   -d               debug mode (pick ray, boxes, click/move log on stderr)
	//   -fen "..."       start from a position instead of the opening setup
	//   -tc N            time control N from the Game Options list (0 = untimed)
	//   -clock S         start each clock at S seconds (short games for testing)
	//   -human | -bot    two players on one screen, or play the computer (default)
	//   -color white|black   your color against the computer (default: alternate)
	//   -botelo N        fix the bot's rating instead of adapting it
	//   -seed N          repeatable bot moves
	//   -profile PATH    where your rating is kept (default %APPDATA%\ChessWithMeMate\profile.txt)
	//   -cam YAW,PITCH   start the camera here (degrees)
	//   -choosepromo -autoflip -nohints   the matching Game Options
	const char* startFen = nullptr;
	const char* startCam = nullptr;
	for( int i = 1; i < argc; i++ )
	{
		std::string a = argv[i];
		bool more = i + 1 < argc;
		if( a == "-d" )						DebugOn = 1;
		else if( a == "-fen" && more )		startFen = argv[++i];
		else if( a == "-tc" && more )		TimeControlNow = glm::clamp( atoi( argv[++i] ), 0, NUM_TIME_CONTROLS - 1 );
		else if( a == "-clock" && more )	StartClockSeconds = std::max( 1, atoi( argv[++i] ) );
		else if( a == "-human" )			Opponent = OPPONENT_HUMAN;
		else if( a == "-bot" )				Opponent = OPPONENT_COMPUTER;
		else if( a == "-color" && more )	{ std::string c = argv[++i]; ColorChoice = ( c == "black" ) ? COLOR_BLACK : ( c == "white" ) ? COLOR_WHITE : COLOR_ALTERNATE; }
		else if( a == "-botelo" && more )	FixedBotElo = glm::clamp( atoi( argv[++i] ), RATING_MIN, RATING_MAX );
		else if( a == "-seed" && more )		BotSeed = (unsigned)atoi( argv[++i] );
		else if( a == "-profile" && more )	ProfilePath = argv[++i];
		else if( a == "-cam" && more )		startCam = argv[++i];
		else if( a == "-choosepromo" )		ChoosePromotion = true;
		else if( a == "-autoflip" )			AutoFlip = true;
		else if( a == "-nohints" )			ShowHints = false;
		else
			fprintf( stderr, "Unknown option '%s'\n", argv[i] );
	}

	// create the display lists that **will not change**:

	InitLists( );

	// your rating, from last time:

	LoadProfile( );

	// set up a new game:

	StartNewGame( startFen );

	float yaw, pitch;
	if( startCam != nullptr && sscanf( startCam, "%f,%f", &yaw, &pitch ) == 2 )
	{
		CamYaw = yaw;
		CamPitch = glm::clamp( pitch, CAM_PITCH_MIN, CAM_PITCH_MAX );
	}

	// make sure a search thread is stopped before the program exits (closing the window
	// exits from inside glut, and a still-running std::thread would abort the process):
	atexit( []( ) { StopBot( ); } );

	// setup all the user interface stuff:

	InitMenus( );

	// draw the scene once and wait for some interaction:
	// (this will never return)

	glutSetWindow( MainWindow );
	glutMainLoop( );

	// glutMainLoop( ) never actually returns
	// the following line is here to make the compiler happy:

	return 0;
}

#pragma endregion

#pragma region Board Helpers

glm::vec3 SquareCenter(int sq)
{
	// white's back rank (rank 1) is nearest the default camera at +z:
	return glm::vec3((FileOf(sq) - 3.5f) * TILE, 0.f, (3.5f - RankOf(sq)) * TILE);
}

bool IsGameOver()
{
	return FlagSide >= 0 || ResignedSide >= 0 || Status == STATUS_CHECKMATE || Status == STATUS_STALEMATE ||
		Status == STATUS_DRAW_FIFTY || Status == STATUS_DRAW_MATERIAL;
}

bool IsTimed()
{
	return TimeControls[TimeControlNow].minutes > 0;
}

// a flag fall is only a loss if the other side could still, in theory, checkmate:
bool HasMatingMaterial(int s)
{
	int minors = 0;
	for (int sq = 0; sq < 64; sq++)
	{
		int8_t pc = Rules.board[sq];
		if (pc == 0 || SideOf(pc) != s)
			continue;
		int t = TypeOf(pc);
		if (t == PAWN || t == ROOK || t == QUEEN)
			return true;
		if (t == KNIGHT || t == BISHOP)
			minors++;
	}
	return minors >= 2;
}

// for a castling move, the square of the rook it uses (clicking the rook castles too); else -1
int CastleRookSquare(const Move& m)
{
	if (!(m.flags & MF_CASTLE))
		return -1;
	int rookFrom, rookTo;
	ChessRules::CastleRookSquares(m, rookFrom, rookTo);
	return rookFrom;
}

bool IsLegalTarget(int to)
{
	for (const Move& m : LegalMoves)
		if (m.from == SelectedSq && (m.to == to || CastleRookSquare(m) == to))
			return true;
	return false;
}

bool HasLegalMoves(int from)
{
	for (const Move& m : LegalMoves)
		if (m.from == from)
			return true;
	return false;
}

// short algebraic-ish text for the HUD, e.g. "Nxf7+", "e8=Q", "O-O"
// (call after the move has been made on Rules)
std::string MoveText(const Move& m)
{
	std::string s;
	if (m.flags & MF_CASTLE)
		s = (m.to > m.from) ? "O-O" : "O-O-O";
	else
	{
		const char* letters = " PNBRQK";
		int type = (m.flags & MF_PROMOTION) ? PAWN : TypeOf(Rules.board[m.to]);
		if (type != PAWN)
			s += letters[type];
		s += SquareName(m.from);
		s += (m.flags & MF_CAPTURE) ? "x" : "-";
		s += SquareName(m.to);
		if (m.flags & MF_PROMOTION)
		{
			s += "=";
			s += letters[m.promo];
		}
	}
	if (Status == STATUS_CHECKMATE)
		s += "#";
	else if (Status == STATUS_CHECK)
		s += "+";
	return s;
}

// after any change to Rules: recompute legal moves and the game status
void RefreshGameState()
{
	Rules.GenerateLegal(LegalMoves);
	Status = Rules.Status();
	const Move* last = Rules.LastMove();
	LastMoveText = last ? MoveText(*last) : "";
	if (IsGameOver())
		ClockRunning = false;
}

bool VsComputer()
{
	return Opponent == OPPONENT_COMPUTER;
}

// turn the board to face whoever is to move (Auto-Flip option, two-player games only --
// against the computer the board stays on your side)
void ApplyAutoFlip()
{
	if (!AutoFlip || VsComputer())
		return;
	float yaw = (Rules.side == SIDE_WHITE) ? 0.f : 180.f;
	if (yaw != CamYaw && DebugOn != 0)
		fprintf(stderr, "view: %s side\n", Rules.side == SIDE_WHITE ? "white" : "black");
	CamYaw = yaw;
}

// snap every on-screen piece to match Rules.board (new game, undo)
void SyncPiecesToBoard()
{
	for (Piece& p : Pieces)
		p = Piece();

	int next[2] = { 0, 16 };	// white pieces use slots 0-15, black 16-31
	for (int sq = 0; sq < 64; sq++)
	{
		PieceAt[sq] = nullptr;
		int8_t pc = Rules.board[sq];
		if (pc == 0)
			continue;
		int s = SideOf(pc);
		if (next[s] >= 16 * (s + 1))
			continue;	// can't happen in a real game -- at most 16 pieces a side

		Piece& p = Pieces[next[s]++];
		p.type = TypeOf(pc);
		p.isWhite = (s == SIDE_WHITE);
		p.square = sq;
		p.visible = true;
		p.pos = SquareCenter(sq);
		PieceAt[sq] = &p;
	}
}

#pragma endregion

#pragma region Clock

// m:ss, or s.t in the last ten seconds
std::string FormatClock(int ms)
{
	char buf[16];
	if (ms < 10000)
		sprintf(buf, "%d.%d", ms / 1000, (ms % 1000) / 100);
	else
	{
		int secs = (ms + 999) / 1000;
		sprintf(buf, "%d:%02d", secs / 60, secs % 60);
	}
	return buf;
}

void ResetClock()
{
	int ms = (StartClockSeconds > 0) ? StartClockSeconds * 1000 : TimeControls[TimeControlNow].minutes * 60 * 1000;
	ClockMs[SIDE_WHITE] = ClockMs[SIDE_BLACK] = ms;
	ClockRunning = false;
	ClockPaused = false;
	FlagSide = -1;
	ClockShown.clear();
}

// charge the side to move for the time since the last update
void UpdateClock()
{
	int now = glutGet(GLUT_ELAPSED_TIME);
	if (ClockRunning && !ClockPaused)
	{
		int& left = ClockMs[Rules.side];
		left -= now - ClockLastMs;
		if (left <= 0)
		{
			left = 0;
			FlagSide = Rules.side;
			ClockRunning = false;
			SelectedSq = -1;
			if (DebugOn != 0)
				fprintf(stderr, "time out: %s\n", FlagSide == SIDE_WHITE ? "white" : "black");
			StopBot();
			RecordResultIfOver();
		}
	}
	ClockLastMs = now;
}

int ClockGeneration;	// bumped on every (re)start so a stale timer chain from an old game dies off

// ticks 10x a second while the clock runs, but only redraws when the shown time changes
void ClockTick(int generation)
{
	if (!ClockRunning || generation != ClockGeneration)
		return;
	UpdateClock();
	std::string shown = FormatClock(ClockMs[Rules.side]);
	if (shown != ClockShown || !ClockRunning)
	{
		ClockShown = shown;
		glutSetWindow(MainWindow);
		glutPostRedisplay();
	}
	if (ClockRunning)
		glutTimerFunc(100, ClockTick, generation);
}

// called right after a move is made (UpdateClock ran just before it, while it was still the
// mover's turn): add the mover's increment and hand the clock to the other side
void PressClock(int mover)
{
	if (!IsTimed() || FlagSide >= 0)
		return;
	ClockLastMs = glutGet(GLUT_ELAPSED_TIME);
	ClockMs[mover] += TimeControls[TimeControlNow].increment * 1000;
	if (IsGameOver())
	{
		ClockRunning = false;
		return;
	}
	if (!ClockRunning)
	{
		ClockRunning = true;
		ClockLastMs = glutGet(GLUT_ELAPSED_TIME);
		glutTimerFunc(100, ClockTick, ++ClockGeneration);
	}
}

#pragma endregion

#pragma region Animation

void StartAnimating()
{
	if (Animating || Frozen)
		return;
	Animating = true;
	LastAnimMs = glutGet(GLUT_ELAPSED_TIME);
	glutIdleFunc(Animate);
}

void StopAnimating()
{
	Animating = false;
	glutIdleFunc(NULL);
}

void StartMove(Piece* p, int toSq, int promoteTo, Piece* victim)
{
	p->moveFrom = p->pos;
	p->moveTo = SquareCenter(toSq);
	float tiles = glm::length(p->moveTo - p->moveFrom) / TILE;
	p->moveTime = std::min(MOVE_TIME_BASE + MOVE_TIME_PER_TILE * tiles, MOVE_TIME_MAX);
	p->hop = (p->type == KNIGHT) ? KNIGHT_HOP_HEIGHT : HOP_HEIGHT;
	p->moveStart = AnimTime;
	p->promoteTo = promoteTo;
	p->victim = victim;
	p->moving = true;
}

void StartDeath(Piece* victim, const glm::vec3& attackerFrom)
{
	// tip over away from whoever took it:
	glm::vec3 dir = victim->pos - attackerFrom;
	dir.y = 0.f;
	if (glm::length(dir) < 0.001f)
		dir = glm::vec3(0.f, 0.f, 1.f);
	victim->fallAxis = glm::cross(glm::vec3(0.f, 1.f, 0.f), glm::normalize(dir));
	victim->deathStart = AnimTime;
	victim->dying = true;
}

void UpdatePiece(Piece& p)
{
	if (p.moving)
	{
		float t = (AnimTime - p.moveStart) / p.moveTime;
		t = glm::clamp(t, 0.f, 1.f);
		float ease = t * t * (3.f - 2.f * t);
		p.pos = glm::mix(p.moveFrom, p.moveTo, ease);
		p.pos.y = p.hop * 4.f * t * (1.f - t);		// little parabola hop

		if (p.victim != nullptr && !p.victim->dying && t >= CONTACT_FRACTION)
			StartDeath(p.victim, p.moveFrom);

		if (t >= 1.f)
		{
			p.moving = false;
			p.pos = p.moveTo;
			if (p.promoteTo != NO_PIECE)
				p.type = p.promoteTo;
			p.promoteTo = NO_PIECE;
			p.victim = nullptr;
		}
	}

	if (p.dying)
	{
		float d = AnimTime - p.deathStart;
		p.opacity = (d <= FALL_TIME) ? 1.f : 1.f - (d - FALL_TIME) / FADE_TIME;
		if (d >= FALL_TIME + FADE_TIME)
		{
			p.dying = false;
			p.visible = false;
			p.opacity = 0.f;
		}
	}
}

// jump every animation straight to its end (used before undo/new moves)
void FinishAnimations()
{
	for (Piece& p : Pieces)
	{
		if (p.moving)
		{
			p.moving = false;
			p.pos = p.moveTo;
			if (p.promoteTo != NO_PIECE)
				p.type = p.promoteTo;
			if (p.victim != nullptr)
				p.victim->visible = false;
			p.promoteTo = NO_PIECE;
			p.victim = nullptr;
		}
		if (p.dying)
		{
			p.dying = false;
			p.visible = false;
		}
	}
	StopAnimating();
}

void FallingTimes()
{
	// the original wobbly topple, scaled to FALL_TIME:
	FallingAnimation.Init();
	FallingAnimation.AddTimeValue(0.0f, 0.0f);
	FallingAnimation.AddTimeValue(1.0f / 6.0f * FALL_TIME, 10.0f);
	FallingAnimation.AddTimeValue(2.0f / 6.0f * FALL_TIME, 45.0f);
	FallingAnimation.AddTimeValue(3.0f / 6.0f * FALL_TIME, 89.0f);
	FallingAnimation.AddTimeValue(4.0f / 6.0f * FALL_TIME, 76.0f);
	FallingAnimation.AddTimeValue(5.0f / 6.0f * FALL_TIME, 89.0f);
	FallingAnimation.AddTimeValue(11.0f / 12.0f * FALL_TIME, 85.0f);
	FallingAnimation.AddTimeValue(FALL_TIME, 90.0f);
}

// this is where one would put code that is to be called
// everytime the glut main loop has nothing to do
//
// the idle function is only installed while pieces are moving,
// so the program sits at ~0% cpu when the board is still
//
// do not call Display( ) from here -- let glutPostRedisplay( ) do it
void
Animate( )
{
	int ms = glutGet(GLUT_ELAPSED_TIME);
	float dt = (ms - LastAnimMs) / 1000.f;
	LastAnimMs = ms;
	if (!Frozen)
		AnimTime += dt;

	bool busy = false;
	for (Piece& p : Pieces)
	{
		UpdatePiece(p);
		busy = busy || p.Busy();
	}
	if (!busy)
	{
		StopAnimating();
		ApplyAutoFlip();	// turn the board only once the pieces have landed
	}

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

#pragma endregion

#pragma region Moves And Selection

void PlayMove(const Move& m)
{
	FinishAnimations();

	// settle the clock while it is still the mover's turn -- they may have just run out:
	if (IsTimed())
	{
		UpdateClock();
		if (FlagSide >= 0)
			return;		// UpdateClock already recorded the result
	}
	int moverSide = Rules.side;

	Piece* mover = PieceAt[m.from];
	Piece* victim = nullptr;
	if (m.flags & MF_ENPASSANT)
		victim = PieceAt[m.to + (mover->isWhite ? -8 : 8)];
	else if (m.flags & MF_CAPTURE)
		victim = PieceAt[m.to];

	if (victim != nullptr)
	{
		PieceAt[victim->square] = nullptr;
		victim->square = -1;
	}

	PieceAt[m.from] = nullptr;
	PieceAt[m.to] = mover;
	mover->square = m.to;
	StartMove(mover, m.to, (m.flags & MF_PROMOTION) ? m.promo : NO_PIECE, victim);

	if (m.flags & MF_CASTLE)
	{
		int rookFrom, rookTo;
		ChessRules::CastleRookSquares(m, rookFrom, rookTo);
		Piece* rook = PieceAt[rookFrom];
		PieceAt[rookFrom] = nullptr;
		PieceAt[rookTo] = rook;
		rook->square = rookTo;
		StartMove(rook, rookTo, NO_PIECE, nullptr);
	}

	Rules.MakeMove(m);
	SelectedSq = -1;
	PendingPromoFrom = PendingPromoTo = -1;
	if (!VsComputer() || moverSide == HumanSide)
		RatingNote.clear();		// last game's rating change has been seen
	RefreshGameState();
	PressClock(moverSide);
	StartAnimating();

	if (DebugOn != 0)
		fprintf(stderr, "%s%s\n", (VsComputer() && moverSide != HumanSide) ? "bot: " : "", LastMoveText.c_str());

	RecordResultIfOver();
	StartBot();		// no-op unless it's now the computer's turn
}

void HandleClick(int sq)
{
	if (DebugOn != 0)
		fprintf(stderr, "click %s\n", sq >= 0 ? SquareName(sq).c_str() : "off board");

	if (IsGameOver() || ClockPaused)
		return;
	if (VsComputer() && Rules.side != HumanSide)
		return;		// the computer is thinking

	// a click anywhere cancels a pending promotion choice:
	PendingPromoFrom = PendingPromoTo = -1;

	if (SelectedSq >= 0 && sq >= 0)
	{
		// legal moves list queens first, so a promotion defaults to a queen:
		for (const Move& m : LegalMoves)
		{
			if (m.from == SelectedSq && (m.to == sq || CastleRookSquare(m) == sq))
			{
				if ((m.flags & MF_PROMOTION) && ChoosePromotion)
				{
					// wait for 1-4 to pick the piece:
					PendingPromoFrom = m.from;
					PendingPromoTo = m.to;
					return;
				}
				PlayMove(m);
				return;
			}
		}
		if (DebugOn != 0 && sq != SelectedSq)
			fprintf(stderr, "not legal: %s-%s\n", SquareName(SelectedSq).c_str(), SquareName(sq).c_str());
	}

	// otherwise (re)select one of the mover's own pieces, or clear the selection:
	bool ownPiece = sq >= 0 && Rules.board[sq] != 0 && SideOf(Rules.board[sq]) == Rules.side;
	SelectedSq = (ownPiece && sq != SelectedSq) ? sq : -1;
}

// 1-4 while a promotion is pending: queen, rook, bishop, knight
void ChoosePromotionPiece(int type)
{
	if (ClockPaused || IsGameOver())
		return;
	for (const Move& m : LegalMoves)
	{
		if (m.from == PendingPromoFrom && m.to == PendingPromoTo && m.promo == type)
		{
			PlayMove(m);
			return;
		}
	}
}

#pragma endregion

#pragma region Computer Opponent

std::string DefaultProfilePath()
{
#ifdef WIN32
	const char* appdata = getenv("APPDATA");
	if (appdata != nullptr)
	{
		std::string dir = std::string(appdata) + "\\ChessWithMeMate";
		_mkdir(dir.c_str());	// fine if it already exists
		return dir + "\\profile.txt";
	}
#endif
	return "profile.txt";
}

// profile.txt is plain "key value" lines, e.g. "rating 1016"
void LoadProfile()
{
	if (ProfilePath.empty())
		ProfilePath = DefaultProfilePath();
	std::ifstream in(ProfilePath);
	std::string key;
	int value;
	while (in >> key >> value)
	{
		if (key == "rating")		Player.rating = glm::clamp(value, RATING_MIN, RATING_MAX);
		else if (key == "games")	Player.games = std::max(0, value);
		else if (key == "wins")		Player.wins = std::max(0, value);
		else if (key == "losses")	Player.losses = std::max(0, value);
		else if (key == "draws")	Player.draws = std::max(0, value);
	}
}

void SaveProfile()
{
	std::ofstream out(ProfilePath);
	out << "rating " << Player.rating << "\n"
		<< "games " << Player.games << "\n"
		<< "wins " << Player.wins << "\n"
		<< "losses " << Player.losses << "\n"
		<< "draws " << Player.draws << "\n";
	if (!out)
		fprintf(stderr, "Could not save profile to %s\n", ProfilePath.c_str());
}

// once a computer game ends: update your rating (standard Elo) and save it
void RecordResultIfOver()
{
	if (!VsComputer() || ResultRecorded || !IsGameOver())
		return;
	ResultRecorded = true;

	double score;	// yours: 1 win, 0.5 draw, 0 loss
	if (ResignedSide >= 0)
		score = (ResignedSide == HumanSide) ? 0.0 : 1.0;
	else if (FlagSide >= 0)
		score = !HasMatingMaterial(FlagSide ^ 1) ? 0.5 : (FlagSide == HumanSide) ? 0.0 : 1.0;
	else if (Status == STATUS_CHECKMATE)
		score = (Rules.side == HumanSide) ? 0.0 : 1.0;	// the side to move is the one mated
	else
		score = 0.5;

	int before = Player.rating;
	Player.rating = AdjustRating(before, BotElo, score, Player.games);
	Player.games++;
	if (score == 1.0)		Player.wins++;
	else if (score == 0.0)	Player.losses++;
	else					Player.draws++;
	SaveProfile();

	char buf[64];
	sprintf(buf, "RATING %d -> %d (%+d)", before, Player.rating, Player.rating - before);
	RatingNote = buf;
	if (DebugOn != 0)
		fprintf(stderr, "result: %s, %s\n", score == 1.0 ? "win" : score == 0.0 ? "loss" : "draw", buf);
}

void StopBot()
{
	if (BotThread.joinable())
	{
		BotStop = true;		// the search checks this and bails out quickly
		BotThread.join();
	}
	BotStop = false;
	BotDone = false;
	BotThinking = false;
	BotGeneration++;
}

const int BOT_MIN_THINK_MS = 600;	// even an instant answer waits a beat, so moves don't blur together

// checks 20x a second for the computer's move; plays it once the board is still
void BotPoll(int generation)
{
	if (generation != BotGeneration || !BotThinking)
		return;
	if (IsGameOver())
	{
		StopBot();
		return;
	}
	bool ready = BotDone && !Animating && !ClockPaused &&
		glutGet(GLUT_ELAPSED_TIME) - BotStartMs >= BOT_MIN_THINK_MS;
	if (!ready)
	{
		glutTimerFunc(50, BotPoll, generation);
		return;
	}
	BotThread.join();
	BotThinking = false;
	Move m = BotResult;
	if (m.from != m.to)
		PlayMove(m);
}

void StartBot()
{
	if (!VsComputer() || Rules.side == HumanSide || IsGameOver() || BotThinking)
		return;
	StopBot();		// joins a finished thread, if one is left over

	BotLevel level = LevelForElo(BotElo);
	if (IsTimed())		// don't let the computer burn more than a small slice of its clock
		level.thinkSeconds = std::min(level.thinkSeconds, ClockMs[Rules.side] / 1000.0 / 30.0 + 0.05);
	unsigned seed = BotSeed != 0 ? BotSeed + (unsigned)Rules.Ply() : std::random_device{}();

	BotThinking = true;
	BotDone = false;
	BotStartMs = glutGet(GLUT_ELAPSED_TIME);
	ChessRules position = Rules;	// the thread gets its own copy
	BotThread = std::thread([position, level, seed]()
	{
		Engine engine;
		BotResult = engine.Choose(position, level, BotStop, seed);
		BotDone = true;
	});
	glutTimerFunc(50, BotPoll, BotGeneration);
}

// walking away from a computer game you've already played a couple of moves in counts as
// resigning it -- otherwise abandoning lost games would keep your rating artificially high
void LeaveCurrentGame()
{
	StopBot();
	if (VsComputer() && !ResultRecorded && !IsGameOver() && Rules.Ply() >= 3)
	{
		ResignedSide = HumanSide;
		if (DebugOn != 0)
			fprintf(stderr, "left an unfinished game: counted as resigning\n");
		RecordResultIfOver();
	}
}

void StartNewGame(const char* fen)
{
	LeaveCurrentGame();
	FinishAnimations();
	Rules.Reset();
	if (fen != nullptr && !Rules.LoadFEN(fen))
	{
		fprintf(stderr, "Bad -fen position, using the normal setup\n");
		Rules.Reset();
	}
	SyncPiecesToBoard();
	SelectedSq = -1;
	PendingPromoFrom = PendingPromoTo = -1;
	ResignedSide = -1;
	ResultRecorded = false;
	ResetClock();		// (RatingNote stays up until you move, so a rating change from leaving a game is seen)

	if (VsComputer())
	{
		if (ColorChoice == COLOR_WHITE)			HumanSide = SIDE_WHITE;
		else if (ColorChoice == COLOR_BLACK)	HumanSide = SIDE_BLACK;
		else HumanSide = (Player.games % 2 == 0) ? SIDE_WHITE : SIDE_BLACK;	// alternate
		BotElo = (FixedBotElo != 0) ? FixedBotElo : BotRatingFor(Player.rating);
		SetView(HumanSide == SIDE_WHITE ? VIEW_WHITE : VIEW_BLACK);
		if (DebugOn != 0)
			fprintf(stderr, "--- new game vs bot %d, you play %s ---\n", BotElo, HumanSide == SIDE_WHITE ? "white" : "black");
	}
	else if (DebugOn != 0)
		fprintf(stderr, "--- new game ---\n");

	RefreshGameState();
	ApplyAutoFlip();
	StartBot();
}

#pragma endregion

#pragma region CameraSet

// build the view and projection matrices once, and use them for both drawing
// and picking so a click always lands where it looks like it does
void UpdateMatrices()
{
	// the viewport is a square centered in the window:
	GLsizei vx = glutGet(GLUT_WINDOW_WIDTH);
	GLsizei vy = glutGet(GLUT_WINDOW_HEIGHT);
	GLsizei v = vx < vy ? vx : vy;			// minimum dimension
	GLint xl = (vx - v) / 2;
	GLint yb = (vy - v) / 2;
	Viewport = glm::vec4(xl, yb, v, v);

	float pitch = glm::radians(CamPitch);
	float yaw = glm::radians(CamYaw);
	CamEye = CamDist * glm::vec3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw));
	ViewMatrix = glm::lookAt(CamEye, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));

	if (NowProjection == ORTHO)
	{
		float h = CamDist * tan(glm::radians(CAM_FOV / 2.f));	// same framing as perspective
		ProjMatrix = glm::ortho(-h, h, -h, h, 1.f, 3000.f);
	}
	else
		ProjMatrix = glm::perspective(glm::radians(CAM_FOV), 1.f, 1.f, 3000.f);
}

void SetView(int view)
{
	CamDist = CAM_DIST;
	CamPitch = (view == VIEW_TOP) ? CAM_PITCH_MAX : CAM_PITCH;
	CamYaw = (view == VIEW_BLACK) ? 180.f : 0.f;
}

#pragma endregion

#pragma region Picking

bool RayHitsBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& bmin, const glm::vec3& bmax, float& tHit)
{
	glm::vec3 invDir = 1.0f / d;
	glm::vec3 t0s = (bmin - o) * invDir;
	glm::vec3 t1s = (bmax - o) * invDir;
	glm::vec3 tmin = glm::min(t0s, t1s);
	glm::vec3 tmax = glm::max(t0s, t1s);
	float tnear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
	float tfar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);
	if (tfar < 0.f || tnear > tfar)
		return false;
	tHit = tnear;
	return true;
}

// Moller-Trumbore ray/triangle test against the piece's actual mesh, so clicks go through
// the empty corners of its bounding box (pieces taper, boxes don't)
bool RayHitsModel(const glm::vec3& o, const glm::vec3& d, const Piece& p, const glm::vec3& base, float& tHit)
{
	// into the model's own coordinates -- black pieces are drawn turned 180 degrees about y:
	glm::vec3 lo = o - base, ld = d;
	if (!p.isWhite)
	{
		lo = glm::vec3(-lo.x, lo.y, -lo.z);
		ld = glm::vec3(-ld.x, ld.y, -ld.z);
	}

	const std::vector<glm::vec3>& tris = PieceTris[p.type];
	float best = 1.e+37f;
	for (size_t i = 0; i + 2 < tris.size(); i += 3)
	{
		glm::vec3 e1 = tris[i + 1] - tris[i];
		glm::vec3 e2 = tris[i + 2] - tris[i];
		glm::vec3 pv = glm::cross(ld, e2);
		float det = glm::dot(e1, pv);
		if (fabs(det) < 1.e-8f)
			continue;
		float inv = 1.f / det;
		glm::vec3 tv = lo - tris[i];
		float u = glm::dot(tv, pv) * inv;
		if (u < 0.f || u > 1.f)
			continue;
		glm::vec3 qv = glm::cross(tv, e1);
		float v = glm::dot(ld, qv) * inv;
		if (v < 0.f || u + v > 1.f)
			continue;
		float t = glm::dot(e2, qv) * inv;
		if (t > 0.f && t < best)
			best = t;
	}
	if (best >= 1.e+37f)
		return false;
	tHit = best;
	return true;
}

void MouseRay(int x, int y, glm::vec3& origin, glm::vec3& dir)
{
	UpdateMatrices();
	float winY = (float)(glutGet(GLUT_WINDOW_HEIGHT) - y);	// glut y runs top-down, OpenGL bottom-up
	glm::vec3 nearPt = glm::unProject(glm::vec3((float)x, winY, 0.f), ViewMatrix, ProjMatrix, Viewport);
	glm::vec3 farPt = glm::unProject(glm::vec3((float)x, winY, 1.f), ViewMatrix, ProjMatrix, Viewport);
	origin = nearPt;
	dir = glm::normalize(farPt - nearPt);
}

// which square is under the mouse? the nearest piece wins, then the board itself
int PickSquare(int x, int y)
{
	glm::vec3 o, d;
	MouseRay(x, y, o, d);
	if (DebugOn != 0)
	{
		RayOrigin = o;
		RayDir = d;
		HaveRay = true;
	}

	float best = 1.e+37f;
	int bestSq = -1;

	if (fabs(d.y) > 1.e-6f)
	{
		float t = -o.y / d.y;
		if (t > 0.f)
		{
			glm::vec3 hit = o + t * d;
			int f = (int)floor(hit.x / TILE + 4.f);
			int r = (int)floor(4.f - hit.z / TILE);
			if (OnBoard(f, r))
			{
				best = t;
				bestSq = MakeSquare(f, r);
			}
		}
	}

	for (const Piece& p : Pieces)
	{
		if (!p.visible || p.square < 0)
			continue;
		glm::vec3 base = p.pos;
		if (p.square == SelectedSq)
			base.y += SELECTED_LIFT;
		float t;
		if (!RayHitsBox(o, d, base + PieceMin[p.type], base + PieceMax[p.type], t) || t >= best)
			continue;	// quick reject: can't hit this piece, or something nearer is already hit
		if (RayHitsModel(o, d, p, base, t) && t < best)
		{
			best = t;
			bestSq = p.square;
		}
	}
	return bestSq;
}

#pragma endregion

#pragma region Draw

void SetPieceMaterial(bool white, float alpha)
{
	// with lighting on, glColor is ignored -- alpha has to go through the material:
	float c = white ? 1.f : 0.f;
	GLfloat color[4] = { c, c, c, alpha };
	GLfloat spec[4] = { .8f, .8f, .8f, alpha };
	GLfloat none[4] = { 0.f, 0.f, 0.f, alpha };
	glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, none);
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, color);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, color);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 70.f);
}

void DrawPiece(const Piece& p)
{
	glPushMatrix();
	glTranslatef(p.pos.x, p.pos.y, p.pos.z);
	if (p.square >= 0 && p.square == SelectedSq)
		glTranslatef(0.f, SELECTED_LIFT, 0.f);
	if (p.dying)
	{
		float angle = FallingAnimation.GetValue(std::min(AnimTime - p.deathStart, FALL_TIME));
		glRotatef(angle, p.fallAxis.x, p.fallAxis.y, p.fallAxis.z);
	}
	if (!p.isWhite)
		glRotatef(180.f, 0.f, 1.f, 0.f);	// knights face each other
	SetPieceMaterial(p.isWhite, p.opacity);
	glCallList(PieceLists[p.type]);
	glPopMatrix();
}

void DrawPieces()
{
	glEnable(GL_LIGHTING);

	// solid pieces first, then fading ones on top without writing depth:
	for (const Piece& p : Pieces)
		if (p.visible && p.opacity >= 1.f)
			DrawPiece(p);

	glDepthMask(GL_FALSE);
	for (const Piece& p : Pieces)
		if (p.visible && p.opacity < 1.f)
			DrawPiece(p);
	glDepthMask(GL_TRUE);

	glDisable(GL_LIGHTING);
}

// a flat square over a board square, scaled to 'size' of a tile
void SquareQuad(int sq, float size)
{
	glm::vec3 c = SquareCenter(sq);
	float h = 0.5f * TILE * size;
	glBegin(GL_QUADS);
	glVertex3f(c.x - h, HIGHLIGHT_Y, c.z - h);
	glVertex3f(c.x - h, HIGHLIGHT_Y, c.z + h);
	glVertex3f(c.x + h, HIGHLIGHT_Y, c.z + h);
	glVertex3f(c.x + h, HIGHLIGHT_Y, c.z - h);
	glEnd();
}

// a hollow square frame around the edge of a board square
void SquareFrame(int sq, float thickness)
{
	glm::vec3 c = SquareCenter(sq);
	float h = 0.5f * TILE;
	float t = TILE * thickness;
	float x0 = c.x - h, x1 = c.x + h, z0 = c.z - h, z1 = c.z + h;
	glBegin(GL_QUADS);
	glVertex3f(x0, HIGHLIGHT_Y, z0);	glVertex3f(x0, HIGHLIGHT_Y, z0 + t);	glVertex3f(x1, HIGHLIGHT_Y, z0 + t);	glVertex3f(x1, HIGHLIGHT_Y, z0);
	glVertex3f(x0, HIGHLIGHT_Y, z1 - t);	glVertex3f(x0, HIGHLIGHT_Y, z1);	glVertex3f(x1, HIGHLIGHT_Y, z1);	glVertex3f(x1, HIGHLIGHT_Y, z1 - t);
	glVertex3f(x0, HIGHLIGHT_Y, z0);	glVertex3f(x0, HIGHLIGHT_Y, z1);	glVertex3f(x0 + t, HIGHLIGHT_Y, z1);	glVertex3f(x0 + t, HIGHLIGHT_Y, z0);
	glVertex3f(x1 - t, HIGHLIGHT_Y, z0);	glVertex3f(x1 - t, HIGHLIGHT_Y, z1);	glVertex3f(x1, HIGHLIGHT_Y, z1);	glVertex3f(x1, HIGHLIGHT_Y, z0);
	glEnd();
}

void DrawHighlights()
{
	glDisable(GL_LIGHTING);

	const Move* last = Rules.LastMove();
	if (last != nullptr)
	{
		// a tint reads well on light squares; the bright frame makes it show on dark ones too
		glColor4f(0.85f, 0.65f, 0.15f, 0.45f);
		SquareQuad(last->from, 1.f);
		SquareQuad(last->to, 1.f);
		glColor4f(1.f, 0.8f, 0.25f, 0.95f);
		SquareFrame(last->from, 0.08f);
		SquareFrame(last->to, 0.08f);
	}

	if (Status == STATUS_CHECK || Status == STATUS_CHECKMATE)
	{
		glColor4f(0.9f, 0.1f, 0.1f, 0.75f);
		SquareQuad(Rules.KingSquare(Rules.side), 1.f);
	}

	if (SelectedSq >= 0)
	{
		glColor4f(1.f, 0.85f, 0.1f, 0.8f);
		SquareQuad(SelectedSq, 1.f);

		for (const Move& m : LegalMoves)
		{
			if (!ShowHints || m.from != SelectedSq)
				continue;
			if (m.flags & MF_CAPTURE)
			{
				glColor4f(0.9f, 0.25f, 0.2f, 0.9f);
				SquareFrame(m.to, 0.12f);
			}
			else
			{
				glColor4f(0.2f, 0.8f, 0.3f, 0.9f);
				SquareQuad(m.to, 0.28f);
				if (m.flags & MF_CASTLE)
					SquareFrame(CastleRookSquare(m), 0.12f);	// the rook can be clicked to castle
			}
		}
	}

	// hover outline only where a click would do something:
	bool hoverUseful = ShowHints && HoverSq >= 0 && !IsGameOver() &&
		((SelectedSq >= 0 && IsLegalTarget(HoverSq)) || HasLegalMoves(HoverSq));
	if (hoverUseful)
	{
		glColor4f(0.3f, 0.8f, 1.f, 0.9f);
		SquareFrame(HoverSq, 0.06f);
	}
}

// one pixel-font character centered on a world position (always faces the screen)
void WorldChar(const glm::vec3& p, char c)
{
	glRasterPos3f(p.x, p.y, p.z);
	glBitmap(0, 0, 0.f, 0.f, -4.5f, -5.f, NULL);	// nudge so the 9x15 glyph is centered
	glutBitmapCharacter(GLUT_BITMAP_9_BY_15, c);
}

// a-h along the white and black edges, 1-8 along both sides:
void DrawCoordinates()
{
	// the glyphs are coplanar with the border, so skip the depth test (it would chew
	// them up); pieces are drawn afterward and still cover any label they stand in front of
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);
	glColor3f(0.9f, 0.85f, 0.7f);
	float out = 4.f * TILE + 0.5f * BORDER;		// middle of the border strip
	for (int i = 0; i < 8; i++)
	{
		float along = (i - 3.5f) * TILE;
		WorldChar(glm::vec3(along, 0.f,  out), (char)('a' + i));
		WorldChar(glm::vec3(along, 0.f, -out), (char)('a' + i));
		WorldChar(glm::vec3(-out, 0.f, -along), (char)('1' + i));
		WorldChar(glm::vec3( out, 0.f, -along), (char)('1' + i));
	}
	glEnable(GL_DEPTH_TEST);
}

void DrawBoundingBox(const glm::vec3& min, const glm::vec3& max)
{
	glBegin(GL_LINE_LOOP);
	glVertex3f(min.x, min.y, min.z);
	glVertex3f(max.x, min.y, min.z);
	glVertex3f(max.x, max.y, min.z);
	glVertex3f(min.x, max.y, min.z);
	glEnd();

	glBegin(GL_LINE_LOOP);
	glVertex3f(min.x, min.y, max.z);
	glVertex3f(max.x, min.y, max.z);
	glVertex3f(max.x, max.y, max.z);
	glVertex3f(min.x, max.y, max.z);
	glEnd();

	glBegin(GL_LINES);
	glVertex3f(min.x, min.y, min.z);
	glVertex3f(min.x, min.y, max.z);
	glVertex3f(max.x, min.y, min.z);
	glVertex3f(max.x, min.y, max.z);
	glVertex3f(max.x, max.y, min.z);
	glVertex3f(max.x, max.y, max.z);
	glVertex3f(min.x, max.y, min.z);
	glVertex3f(min.x, max.y, max.z);
	glEnd();
}

void DrawDebug()
{
	glDisable(GL_LIGHTING);
	glColor3f(1.f, 0.f, 0.f);
	for (const Piece& p : Pieces)
		if (p.visible && p.square >= 0)
			DrawBoundingBox(p.pos + PieceMin[p.type], p.pos + PieceMax[p.type]);

	if (HaveRay)
	{
		glm::vec3 end = RayOrigin + RayDir * 3000.f;
		glBegin(GL_LINES);
		glVertex3f(RayOrigin.x, RayOrigin.y, RayOrigin.z);
		glVertex3f(end.x, end.y, end.z);
		glEnd();
	}
}

// pixel font text with a 1-step drop shadow, in 0-100 "percent" screen units
void HudText(float x, float y, const std::string& s, float r, float g, float b)
{
	glColor3f(0.f, 0.f, 0.f);
	DoRasterString(x + 0.25f, y - 0.25f, 0.f, (char*)s.c_str());
	glColor3f(r, g, b);
	DoRasterString(x, y, 0.f, (char*)s.c_str());
}

void DrawHud()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0.f, 100.f, 0.f, 100.f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// against the computer, talk about "you" and "the bot" instead of white and black:
	bool bot = VsComputer();
	auto Name = [bot](int s) -> std::string
	{
		if (bot)
			return (s == HumanSide) ? "YOU" : "BOT";
		return (s == SIDE_WHITE) ? "WHITE" : "BLACK";
	};
	auto Wins = [bot, Name](int s) -> std::string
	{
		return (bot && s == HumanSide) ? "YOU WIN" : Name(s) + " WINS";
	};
	std::string toMove = bot ? (Rules.side == HumanSide ? "YOUR MOVE" : "BOT THINKING...") : Name(Rules.side) + " TO MOVE";

	std::string line;
	float r = 1.f, g = 1.f, b = 1.f;
	switch (Status)
	{
	case STATUS_PLAYING:		line = toMove; break;
	case STATUS_CHECK:			line = toMove + " - CHECK!"; r = 1.f; g = 0.35f; b = 0.3f; break;
	case STATUS_CHECKMATE:		line = "CHECKMATE - " + Wins(Rules.side ^ 1) + "   [N] NEW GAME"; r = 1.f; g = 0.85f; b = 0.1f; break;
	case STATUS_STALEMATE:		line = "STALEMATE - DRAW   [N] NEW GAME"; r = 0.6f; g = 0.9f; b = 1.f; break;
	case STATUS_DRAW_FIFTY:		line = "DRAW - FIFTY MOVE RULE   [N] NEW GAME"; r = 0.6f; g = 0.9f; b = 1.f; break;
	case STATUS_DRAW_MATERIAL:	line = "DRAW - NOT ENOUGH MATERIAL   [N] NEW GAME"; r = 0.6f; g = 0.9f; b = 1.f; break;
	}
	if (FlagSide >= 0)
	{
		if (HasMatingMaterial(FlagSide ^ 1))
			line = Name(FlagSide) + " OUT OF TIME - " + Wins(FlagSide ^ 1);
		else
			line = Name(FlagSide) + " OUT OF TIME - DRAW";
		r = 1.f; g = 0.85f; b = 0.1f;
	}
	if (ResignedSide >= 0)
	{
		line = Name(ResignedSide) + " RESIGNED - " + Wins(ResignedSide ^ 1) + "   [N] NEW GAME";
		r = 1.f; g = 0.85f; b = 0.1f;
	}
	HudText(2.f, 96.f, line, r, g, b);

	// the bot's level, in US Chess terms, and yours:
	if (bot)
	{
		char buf[96];
		sprintf(buf, "BOT %d %s   YOU %d %s", BotElo, RatingClass(BotElo), Player.rating, RatingClass(Player.rating));
		HudText(2.f, 92.5f, buf, 0.6f, 0.9f, 1.f);
	}

	float y = bot ? 89.f : 92.5f;
	if (!LastMoveText.empty())
	{
		HudText(2.f, y, "LAST: " + LastMoveText, 0.85f, 0.85f, 0.85f);
		y -= 3.5f;
	}

	if (!RatingNote.empty())
	{
		HudText(2.f, y, RatingNote, 1.f, 0.85f, 0.1f);
		y -= 3.5f;
	}

	if (PendingPromoTo >= 0)
	{
		HudText(2.f, y, "PROMOTE TO: [1] QUEEN [2] ROOK [3] BISHOP [4] KNIGHT", 1.f, 0.85f, 0.1f);
		y -= 3.5f;
	}

	// clocks, top right -- the side to move is bright, under 10 seconds turns red:
	if (IsTimed())
	{
		for (int s = SIDE_WHITE; s <= SIDE_BLACK; s++)
		{
			bool active = (s == Rules.side) && !IsGameOver();
			float cr = active ? 1.f : 0.6f, cg = active ? 1.f : 0.6f, cb = active ? 1.f : 0.6f;
			if (ClockMs[s] < 10000)
			{
				cr = 1.f; cg = 0.3f; cb = 0.3f;
			}
			std::string label = (s == SIDE_WHITE) ? "WHITE " : "BLACK ";
			HudText(76.f, (s == SIDE_BLACK) ? 96.f : 92.5f, (active ? ">" : " ") + label + FormatClock(ClockMs[s]), cr, cg, cb);
		}
		if (ClockPaused)
			HudText(76.f, 89.f, " PAUSED [P]", 0.6f, 0.9f, 1.f);
	}

	if (Frozen)
		HudText(2.f, y, "ANIMATION FROZEN [F]", 0.6f, 0.9f, 1.f);

	if (IsTimed())
		HudText(2.f, 2.f, "[N]EW [P]AUSE [V]IEW  SHIFT+DRAG/ARROWS ORBIT  WHEEL ZOOM", 0.75f, 0.75f, 0.75f);
	else
		HudText(2.f, 2.f, "[N]EW [U]NDO [V]IEW  SHIFT+DRAG/ARROWS ORBIT  WHEEL ZOOM", 0.75f, 0.75f, 0.75f);
}

// draw the complete scene:
void
Display( )
{
	// set which window we want to do the graphics into:
	glutSetWindow( MainWindow );

	// erase the background:
	glDrawBuffer( GL_BACK );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	glEnable( GL_DEPTH_TEST );

	// flat shading keeps the low-poly, pixelly look:
	glShadeModel( GL_FLAT );

	UpdateMatrices();
	glViewport((GLint)Viewport.x, (GLint)Viewport.y, (GLsizei)Viewport.z, (GLsizei)Viewport.w);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(glm::value_ptr(ProjMatrix));
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(glm::value_ptr(ViewMatrix));

	// set the fog parameters:
	if( DepthCueOn != 0 )
	{
		const GLfloat FOGCOLOR[4] = { GRAY[0], GRAY[1], GRAY[2], 1.f };
		glFogi( GL_FOG_MODE, GL_LINEAR );
		glFogfv( GL_FOG_COLOR, FOGCOLOR );
		glFogf( GL_FOG_START, CamDist - 100.f );
		glFogf( GL_FOG_END, CamDist + 250.f );
		glEnable( GL_FOG );
	}
	else
	{
		glDisable( GL_FOG );
	}

	// the light is placed after the view matrix so it stays put in the world:
	const GLfloat* lc = Colors[LightColorNow];
	if (isSpotLight)
		SetSpotLight(GL_LIGHT0, LIGHT_RADIUS, LIGHT_HEIGHT, 0.f, 0.f, -1.f, 0.f, lc[0], lc[1], lc[2]);
	else
		SetPointLight(GL_LIGHT0, LIGHT_RADIUS, LIGHT_HEIGHT, 0.f, lc[0], lc[1], lc[2]);
	glEnable( GL_NORMALIZE );

	glDisable(GL_LIGHTING);
	if( AxesOn != 0 )
	{
		glColor3fv( &Colors[NowColor][0] );
		glCallList( AxesList );
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glCallList(BoardList);
	DrawCoordinates();
	DrawHighlights();
	DrawPieces();

	if (DebugOn != 0)
		DrawDebug();

	glDisable(GL_FOG);
	DrawHud();

	// swap the double-buffered framebuffers:
	glutSwapBuffers( );

	// be sure the graphics buffer has been sent:
	// note: be sure to use glFlush( ) here, not glFinish( ) !
	glFlush( );
}

#pragma endregion

#pragma region MenuMethods

void
DoAxesMenu( int id )
{
	AxesOn = id;

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}


void
DoColorMenu( int id )
{
	NowColor = id - RED;

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}


void
DoDebugMenu( int id )
{
	DebugOn = id;
	HaveRay = false;

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}


void
DoDepthMenu( int id )
{
	DepthCueOn = id;

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}


// main menu callback:
void
DoMainMenu( int id )
{
	switch( id )
	{
		case RESET:
			Reset( );
			break;

		case NEW_GAME:
			StartNewGame( nullptr );
			break;

		case UNDO:
		{
			// no take-backs against the clock, or once a game is over:
			FinishAnimations( );
			if( IsTimed( ) || IsGameOver( ) || !Rules.CanUndo( ) )
				break;
			StopBot( );
			// against the computer, go back to your last turn (its reply too, if it made one):
			int plies = 1;
			if( VsComputer( ) && Rules.side == HumanSide && Rules.Ply( ) >= 2 )
				plies = 2;
			for( int i = 0; i < plies && Rules.CanUndo( ); i++ )
			{
				if( DebugOn != 0 )
					fprintf( stderr, "undo %s\n", LastMoveText.c_str( ) );
				Rules.UnmakeMove( );
				RefreshGameState( );
			}
			SyncPiecesToBoard( );
			SelectedSq = -1;
			PendingPromoFrom = PendingPromoTo = -1;
			ApplyAutoFlip( );
			StartBot( );	// e.g. undone back to the start with the computer playing white
			break;
		}

		case RESIGN:
			if( !IsGameOver( ) )
			{
				StopBot( );
				ResignedSide = VsComputer( ) ? HumanSide : Rules.side;
				ClockRunning = false;
				SelectedSq = -1;
				if( DebugOn != 0 )
					fprintf( stderr, "resign: %s\n", ResignedSide == SIDE_WHITE ? "white" : "black" );
				RecordResultIfOver( );
			}
			break;

		case QUIT:
			// gracefully close out the graphics:
			// gracefully close the graphics window:
			// gracefully exit the program:
			StopBot( );
			glutSetWindow( MainWindow );
			glFinish( );
			glutDestroyWindow( MainWindow );
			exit( 0 );
			break;

		default:
			fprintf( stderr, "Don't know what to do with Main Menu ID %d\n", id );
	}

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}


void
DoProjectMenu( int id )
{
	NowProjection = id;

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}


void
DoViewMenu( int id )
{
	SetView( id );

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

// glut menus have no check marks, so put a '*' in front of the current choice:
std::string MenuLabel( const char* text, bool current )
{
	return std::string( current ? "* " : "  " ) + text;
}

void
RefreshOptionMenus( )
{
	int was = glutGetMenu( );

	glutSetMenu( TimeMenu );
	for( int i = 0; i < NUM_TIME_CONTROLS; i++ )
		glutChangeToMenuEntry( i + 1, MenuLabel( TimeControls[i].name, i == TimeControlNow ).c_str( ), i );

	glutSetMenu( HintsMenu );
	glutChangeToMenuEntry( 1, MenuLabel( "On",  ShowHints ).c_str( ), 1 );
	glutChangeToMenuEntry( 2, MenuLabel( "Off", !ShowHints ).c_str( ), 0 );

	glutSetMenu( FlipMenu );
	glutChangeToMenuEntry( 1, MenuLabel( "On",  AutoFlip ).c_str( ), 1 );
	glutChangeToMenuEntry( 2, MenuLabel( "Off", !AutoFlip ).c_str( ), 0 );

	glutSetMenu( PromoMenu );
	glutChangeToMenuEntry( 1, MenuLabel( "Always Queen", !ChoosePromotion ).c_str( ), 0 );
	glutChangeToMenuEntry( 2, MenuLabel( "Choose (1-4)",  ChoosePromotion ).c_str( ), 1 );

	glutSetMenu( OpponentMenu );
	glutChangeToMenuEntry( 1, MenuLabel( "Computer (adapts to you)", Opponent == OPPONENT_COMPUTER ).c_str( ), OPPONENT_COMPUTER );
	glutChangeToMenuEntry( 2, MenuLabel( "Two Players",              Opponent == OPPONENT_HUMAN ).c_str( ),    OPPONENT_HUMAN );

	glutSetMenu( ColorMenu );
	glutChangeToMenuEntry( 1, MenuLabel( "Alternate", ColorChoice == COLOR_ALTERNATE ).c_str( ), COLOR_ALTERNATE );
	glutChangeToMenuEntry( 2, MenuLabel( "White",     ColorChoice == COLOR_WHITE ).c_str( ),     COLOR_WHITE );
	glutChangeToMenuEntry( 3, MenuLabel( "Black",     ColorChoice == COLOR_BLACK ).c_str( ),     COLOR_BLACK );

	if( was != 0 )
		glutSetMenu( was );
}

// picking a time control starts a new game with it:
void
DoTimeMenu( int id )
{
	TimeControlNow = id;
	RefreshOptionMenus( );
	DoMainMenu( NEW_GAME );
}

void
DoHintsMenu( int id )
{
	ShowHints = ( id != 0 );
	RefreshOptionMenus( );
	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

void
DoFlipMenu( int id )
{
	AutoFlip = ( id != 0 );
	ApplyAutoFlip( );
	RefreshOptionMenus( );
	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

void
DoPromoMenu( int id )
{
	ChoosePromotion = ( id != 0 );
	RefreshOptionMenus( );
}

// switching opponent or color starts a new game:
void
DoOpponentMenu( int id )
{
	if( id == Opponent )
		return;
	LeaveCurrentGame( );		// while Opponent still says what kind of game it was
	Opponent = id;
	RefreshOptionMenus( );
	StartNewGame( nullptr );
	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

void
DoYourColorMenu( int id )
{
	if( id == ColorChoice )
		return;
	ColorChoice = id;
	RefreshOptionMenus( );
	StartNewGame( nullptr );
	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

#pragma endregion

#pragma region ToBeOrganizedMiscMethods
// use glut to display a string of characters using a raster font:
void
DoRasterString( float x, float y, float z, char *s )
{
	glRasterPos3f( (GLfloat)x, (GLfloat)y, (GLfloat)z );

	char c;			// one character to print
	for( ; ( c = *s ) != '\0'; s++ )
	{
		glutBitmapCharacter( GLUT_BITMAP_9_BY_15, c );
	}
}

#pragma endregion

#pragma region initMethods

// initialize the glui window:
void
InitMenus( )
{
	glutSetWindow( MainWindow );

	int numColors = sizeof( Colors ) / ( 3*sizeof(float) );
	int colormenu = glutCreateMenu( DoColorMenu );
	for( int i = 0; i < numColors; i++ )
	{
		glutAddMenuEntry( ColorNames[i], i );
	}

	int axesmenu = glutCreateMenu( DoAxesMenu );
	glutAddMenuEntry( "Off",  0 );
	glutAddMenuEntry( "On",   1 );

	int depthcuemenu = glutCreateMenu( DoDepthMenu );
	glutAddMenuEntry( "Off",  0 );
	glutAddMenuEntry( "On",   1 );

	int debugmenu = glutCreateMenu( DoDebugMenu );
	glutAddMenuEntry( "Off",  0 );
	glutAddMenuEntry( "On",   1 );

	int projmenu = glutCreateMenu( DoProjectMenu );
	glutAddMenuEntry( "Orthographic",  ORTHO );
	glutAddMenuEntry( "Perspective",   PERSP );

	int viewmenu = glutCreateMenu( DoViewMenu );
	glutAddMenuEntry( "White Side", VIEW_WHITE );
	glutAddMenuEntry( "Black Side", VIEW_BLACK );
	glutAddMenuEntry( "Top Down",   VIEW_TOP );

	// game options -- entries get relabeled by RefreshOptionMenus( ):
	TimeMenu = glutCreateMenu( DoTimeMenu );
	for( int i = 0; i < NUM_TIME_CONTROLS; i++ )
		glutAddMenuEntry( TimeControls[i].name, i );

	HintsMenu = glutCreateMenu( DoHintsMenu );
	glutAddMenuEntry( "On",  1 );
	glutAddMenuEntry( "Off", 0 );

	FlipMenu = glutCreateMenu( DoFlipMenu );
	glutAddMenuEntry( "On",  1 );
	glutAddMenuEntry( "Off", 0 );

	PromoMenu = glutCreateMenu( DoPromoMenu );
	glutAddMenuEntry( "Always Queen", 0 );
	glutAddMenuEntry( "Choose (1-4)", 1 );

	OpponentMenu = glutCreateMenu( DoOpponentMenu );
	glutAddMenuEntry( "Computer (adapts to you)", OPPONENT_COMPUTER );
	glutAddMenuEntry( "Two Players",              OPPONENT_HUMAN );

	ColorMenu = glutCreateMenu( DoYourColorMenu );
	glutAddMenuEntry( "Alternate", COLOR_ALTERNATE );
	glutAddMenuEntry( "White",     COLOR_WHITE );
	glutAddMenuEntry( "Black",     COLOR_BLACK );

	int optionsmenu = glutCreateMenu( DoMainMenu );
	glutAddSubMenu( "Opponent",        OpponentMenu );
	glutAddSubMenu( "Your Color",      ColorMenu );
	glutAddSubMenu( "Time Control",    TimeMenu );
	glutAddSubMenu( "Move Hints",      HintsMenu );
	glutAddSubMenu( "Auto-Flip Board", FlipMenu );
	glutAddSubMenu( "Promotion",       PromoMenu );

	int mainmenu = glutCreateMenu( DoMainMenu );
	glutAddMenuEntry( "New Game",      NEW_GAME );
	glutAddMenuEntry( "Undo Move",     UNDO );
	glutAddMenuEntry( "Resign",        RESIGN );
	glutAddSubMenu(   "Game Options",  optionsmenu );
	glutAddSubMenu(   "View",          viewmenu );
	glutAddSubMenu(   "Projection",    projmenu );
	glutAddSubMenu(   "Axes",          axesmenu );
	glutAddSubMenu(   "Axis Colors",   colormenu );
	glutAddSubMenu(   "Depth Cue",     depthcuemenu );
	glutAddSubMenu(   "Debug",         debugmenu );
	glutAddMenuEntry( "Reset View",    RESET );
	glutAddMenuEntry( "Quit",          QUIT );

// attach the pop-up menu to the right mouse button:

	glutAttachMenu( GLUT_RIGHT_BUTTON );

	RefreshOptionMenus( );
}

// initialize the glut and OpenGL libraries:
//	also setup callback functions
void
InitGraphics( )
{
	// request the display modes:
	// ask for red-green-blue-alpha color, double-buffering, and z-buffering:

	glutInitDisplayMode( GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH );

	// set the initial window configuration:
	glutInitWindowPosition( 0, 0 );
	glutInitWindowSize( viewportWidth, viewportHeight );

	// open the window and set its title:
	MainWindow = glutCreateWindow( WINDOWTITLE );
	glutSetWindowTitle( WINDOWTITLE );

	// set the framebuffer clear values:
	glClearColor( GRAY[0], GRAY[1], GRAY[2], GRAY[3] );

	glutSetWindow( MainWindow );
	glutDisplayFunc( Display );
	glutReshapeFunc( Resize );
	glutKeyboardFunc( Keyboard );
	glutSpecialFunc( SpecialKeys );
	glutMouseFunc( MouseButton );
	glutMotionFunc( MouseMotion );
	glutPassiveMotionFunc( MousePassive );
	glutVisibilityFunc( Visibility );

	// no idle function until something needs animating:
	glutIdleFunc( NULL );

	FallingTimes();

	// init the glew package (a window must be open to do this):

#ifdef WIN32
	GLenum err = glewInit( );
	if( err != GLEW_OK )
	{
		fprintf( stderr, "glewInit Error\n" );
	}
#endif
}

// initialize the display lists that will not change:
// (a display list is a way to store opengl commands in
//  memory so that they can be played back efficiently at a later time
//  with a call to glCallList( )
void
InitLists( )
{
	glutSetWindow( MainWindow );

	// one list per piece type -- the material is set per piece at draw time
	for (int type = PAWN; type <= KING; type++)
	{
		std::string file = "pieces/" + PieceTypeToString(type) + ".obj";
		glm::vec3 center;
		PieceLists[type] = glGenLists(1);
		glNewList(PieceLists[type], GL_COMPILE);
		ObjTriangleSink = &PieceTris[type];
		LoadObjFile(&file[0], PieceMin[type], PieceMax[type], center);
		ObjTriangleSink = nullptr;
		glEndList();
	}

	// the checkerboard, sitting on a dark border that the coordinates are written on:
	BoardList = glGenLists(1);
	glNewList(BoardList, GL_COMPILE);
	glBegin(GL_QUADS);
	float edge = 4.f * TILE + BORDER;
	glColor3f(0.22f, 0.16f, 0.1f);
	glVertex3f(-edge, -0.1f, -edge);
	glVertex3f(-edge, -0.1f,  edge);
	glVertex3f( edge, -0.1f,  edge);
	glVertex3f( edge, -0.1f, -edge);
	for (int sq = 0; sq < 64; sq++)
	{
		bool light = (FileOf(sq) + RankOf(sq)) % 2 == 1;	// a1 is dark
		float c = light ? 1.0f : 0.1f;
		glColor3f(c, c, c);
		glm::vec3 p = SquareCenter(sq);
		float h = 0.5f * TILE;
		glVertex3f(p.x - h, 0.f, p.z - h);
		glVertex3f(p.x - h, 0.f, p.z + h);
		glVertex3f(p.x + h, 0.f, p.z + h);
		glVertex3f(p.x + h, 0.f, p.z - h);
	}
	glEnd();
	glEndList();

	// create the axes:
	AxesList = glGenLists( 1 );
	glNewList( AxesList, GL_COMPILE );
		glLineWidth( AXES_WIDTH );
			Axes( 100 );
		glLineWidth( 1. );
	glEndList( );
}
#pragma endregion

#pragma region Input

// the keyboard callback:
void
Keyboard( unsigned char c, int x, int y )
{
	// while a promotion is pending, 1-4 (or q/r/b/n) pick the piece and Esc cancels:
	if( PendingPromoTo >= 0 )
	{
		int type = NO_PIECE;
		switch( tolower( c ) )
		{
			case '1': case 'q':	type = QUEEN;	break;
			case '2': case 'r':	type = ROOK;	break;
			case '3': case 'b':	type = BISHOP;	break;
			case '4': case 'n':	type = KNIGHT;	break;
		}
		if( type != NO_PIECE )
			ChoosePromotionPiece( type );
		else if( c == ESCAPE )
			PendingPromoFrom = PendingPromoTo = -1;
		glutSetWindow( MainWindow );
		glutPostRedisplay( );
		return;
	}

	switch( c )
	{
		case 'p':
		case 'P':
			if( IsTimed( ) && !IsGameOver( ) )
			{
				UpdateClock( );			// charge time up to the pause
				ClockPaused = !ClockPaused;
				SelectedSq = -1;
			}
			break;

		case 'n':
		case 'N':
			DoMainMenu( NEW_GAME );
			break;

		case 'u':
		case 'U':
			DoMainMenu( UNDO );
			break;

		case 'v':
		case 'V':
			// flip to the other side of the board:
			SetView( (cos(glm::radians(CamYaw)) > 0.f) ? VIEW_BLACK : VIEW_WHITE );
			break;

		case 'f':
		case 'F':
			Frozen = !Frozen;
			if (Frozen)
				StopAnimating();
			else
				StartAnimating();
			break;

		case 'l':
		case 'L':
			isSpotLight = !isSpotLight;
			break;

		case 'd':
		case 'D':
			DoDebugMenu( DebugOn == 0 ? 1 : 0 );
			break;

		case '+':
		case '=':
			CamDist = std::max(CamDist / ZOOM_FACTOR, CAM_DIST_MIN);
			break;

		case '-':
		case '_':
			CamDist = std::min(CamDist * ZOOM_FACTOR, CAM_DIST_MAX);
			break;

		case 'q':
		case 'Q':
		case ESCAPE:
			DoMainMenu( QUIT );	// will not return here
			break;				// happy compiler

		case 'w':
			LightColorNow = LIGHT_WHITE;
			break;

		case 'r':
			LightColorNow = RED;
			break;

		case 'g':
			LightColorNow = GREEN;
			break;

		case 'b':
			LightColorNow = BLUE;
			break;

		case 'y':
			LightColorNow = YELLOW;
			break;

		default:
			return;		// nothing changed, no need to redraw
	}

	// force a call to Display( ):

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

// arrow keys orbit the camera:
void
SpecialKeys( int key, int x, int y )
{
	switch( key )
	{
		case GLUT_KEY_LEFT:		CamYaw -= ORBIT_KEY_STEP;	break;
		case GLUT_KEY_RIGHT:	CamYaw += ORBIT_KEY_STEP;	break;
		case GLUT_KEY_UP:		CamPitch = std::min(CamPitch + ORBIT_KEY_STEP, CAM_PITCH_MAX);	break;
		case GLUT_KEY_DOWN:		CamPitch = std::max(CamPitch - ORBIT_KEY_STEP, CAM_PITCH_MIN);	break;
		default:				return;
	}

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

// called when the mouse button transitions down or up:
void
MouseButton( int button, int state, int x, int y )
{
	if( state == GLUT_DOWN )
	{
		switch( button )
		{
			case GLUT_LEFT_BUTTON:
				if( glutGetModifiers( ) & GLUT_ACTIVE_SHIFT )
					ActiveButton = 1;	// shift + drag orbits the camera
				else
					HandleClick( PickSquare( x, y ) );
				break;

			case SCROLL_WHEEL_UP:
				CamDist = std::max(CamDist / ZOOM_FACTOR, CAM_DIST_MIN);
				break;

			case SCROLL_WHEEL_DOWN:
				CamDist = std::min(CamDist * ZOOM_FACTOR, CAM_DIST_MAX);
				break;
		}
		Xmouse = x;
		Ymouse = y;
	}
	else
	{
		ActiveButton = 0;
	}

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

// called when the mouse moves while a button is down:
void
MouseMotion( int x, int y )
{
	if( ActiveButton != 0 )
	{
		CamYaw += ORBIT_SPEED * ( x - Xmouse );
		CamPitch = glm::clamp( CamPitch + ORBIT_SPEED * ( y - Ymouse ), CAM_PITCH_MIN, CAM_PITCH_MAX );
		glutSetWindow( MainWindow );
		glutPostRedisplay( );
	}

	Xmouse = x;
	Ymouse = y;
}

// called when the mouse moves with no button down -- only redraw if the hovered square changes
void
MousePassive( int x, int y )
{
	int sq = PickSquare( x, y );
	if( sq != HoverSq || DebugOn != 0 )
	{
		HoverSq = sq;
		glutSetWindow( MainWindow );
		glutPostRedisplay( );
	}
}

#pragma endregion

// reset the view and display options (not the game):
// this only sets the global variables --
// the glut main loop is responsible for redrawing the scene
void
Reset( )
{
	ActiveButton = 0;
	AxesOn = 0;
	DebugOn = 0;
	DepthCueOn = 0;
	Frozen = false;
	HaveRay = false;
	NowColor = YELLOW;
	NowProjection = PERSP;
	LightColorNow = LIGHT_WHITE;
	isSpotLight = false;
	SetView( ( VsComputer( ) && HumanSide == SIDE_BLACK ) ? VIEW_BLACK : VIEW_WHITE );
}

// called when user resizes the window:
void
Resize( int width, int height )
{
	// don't really need to do anything since window size is
	// checked each time in Display( ):

	glutSetWindow( MainWindow );
	glutPostRedisplay( );
}

// handle a change to the window's visibility:
void
Visibility ( int state )
{
	if( state == GLUT_VISIBLE )
	{
		glutSetWindow( MainWindow );
		glutPostRedisplay( );
	}
}

///////////////////////////////////////   HANDY UTILITIES:  //////////////////////////

#pragma region  the stroke characters 'X' 'Y' 'Z' :

static float xx[ ] = { 0.f, 1.f, 0.f, 1.f };

static float xy[ ] = { -.5f, .5f, .5f, -.5f };

static int xorder[ ] = { 1, 2, -3, 4 };

static float yx[ ] = { 0.f, 0.f, -.5f, .5f };

static float yy[ ] = { 0.f, .6f, 1.f, 1.f };

static int yorder[ ] = { 1, 2, 3, -2, 4 };

static float zx[ ] = { 1.f, 0.f, 1.f, 0.f, .25f, .75f };

static float zy[ ] = { .5f, .5f, -.5f, -.5f, 0.f, 0.f };

static int zorder[ ] = { 1, 2, 3, 4, -5, 6 };

// fraction of the length to use as height of the characters:
const float LENFRAC = 0.10f;

// fraction of length to use as start location of the characters:
const float BASEFRAC = 1.10f;

#pragma endregion

//	Draw a set of 3D axes:
//	(length is the axis length in world coordinates)
void
Axes( float length )
{
	glBegin( GL_LINE_STRIP );
		glVertex3f( length, 0., 0. );
		glVertex3f( 0., 0., 0. );
		glVertex3f( 0., length, 0. );
	glEnd( );
	glBegin( GL_LINE_STRIP );
		glVertex3f( 0., 0., 0. );
		glVertex3f( 0., 0., length );
	glEnd( );

	float fact = LENFRAC * length;
	float base = BASEFRAC * length;

	glBegin( GL_LINE_STRIP );
		for( int i = 0; i < 4; i++ )
		{
			int j = xorder[i];
			if( j < 0 )
			{

				glEnd( );
				glBegin( GL_LINE_STRIP );
				j = -j;
			}
			j--;
			glVertex3f( base + fact*xx[j], fact*xy[j], 0.0 );
		}
	glEnd( );

	glBegin( GL_LINE_STRIP );
		for( int i = 0; i < 5; i++ )
		{
			int j = yorder[i];
			if( j < 0 )
			{

				glEnd( );
				glBegin( GL_LINE_STRIP );
				j = -j;
			}
			j--;
			glVertex3f( fact*yx[j], base + fact*yy[j], 0.0 );
		}
	glEnd( );

	glBegin( GL_LINE_STRIP );
		for( int i = 0; i < 6; i++ )
		{
			int j = zorder[i];
			if( j < 0 )
			{

				glEnd( );
				glBegin( GL_LINE_STRIP );
				j = -j;
			}
			j--;
			glVertex3f( 0.0, fact*zy[j], base + fact*zx[j] );
		}
	glEnd( );

}

#pragma region CustomMathMethods

	void
	Cross(float v1[3], float v2[3], float vout[3])
	{
		float tmp[3]{};
		tmp[0] = v1[1] * v2[2] - v2[1] * v1[2];
		tmp[1] = v2[0] * v1[2] - v1[0] * v2[2];
		tmp[2] = v1[0] * v2[1] - v2[0] * v1[1];
		vout[0] = tmp[0];
		vout[1] = tmp[1];
		vout[2] = tmp[2];
	}


	float
	Unit(float vin[3], float vout[3])
	{
		float dist = vin[0] * vin[0] + vin[1] * vin[1] + vin[2] * vin[2];
		if (dist > 0.0)
		{
			dist = sqrtf(dist);
			vout[0] = vin[0] / dist;
			vout[1] = vin[1] / dist;
			vout[2] = vin[2] / dist;
		}
		else
		{
			vout[0] = vin[0];
			vout[1] = vin[1];
			vout[2] = vin[2];
		}
		return dist;
	}

#pragma endregion
