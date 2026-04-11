// emcc pips_wasm.cpp -O2 -o pips.js -s EXPORTED_FUNCTIONS="['_SolvePuzzleStr','_malloc','_free']" -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']" -s MODULARIZE=1 -s EXPORT_NAME="PipsModule" -s ALLOW_MEMORY_GROWTH=1 -s TOTAL_STACK=1048576

#include <stdint.h>
#include <emscripten/emscripten.h>

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef int16_t   i16;
typedef uint32_t  u32;
typedef int32_t   i32;
typedef uint64_t  u64;
typedef double    f64;
typedef u8        b8;
typedef uintptr_t uptr;
typedef intptr_t  iptr;

#define TRUE  1
#define FALSE 0

#define MAX_GRID_W      20
#define MAX_GRID_H      20
#define MAX_CELLS       (MAX_GRID_W * MAX_GRID_H)
#define MAX_DOMINOES    100
#define MAX_REGIONS     64
#define MAX_COLS        (MAX_CELLS + MAX_DOMINOES)
#define MAX_ROWS        10000
#define MAX_DLX_NODES   (MAX_ROWS * 3)

enum RuleType {
    RULE_NONE,
    RULE_EQ, 
    RULE_NEQ, 
    RULE_GT, 
    RULE_LT, 
    RULE_SUM
};

struct Region {
    u8  Type;
    u8  Target;
    u8  TotalCells;
    u8  FilledCells;
    u8  Sum;
    u8  FirstVal;
    u16 Mask;
};

struct RowMeta {
    u8 R1;
    u8 C1; 
    u8 Val1;
    u8 R2;
    u8 C2;
    u8 Val2;
    u8 DominoID;
};

struct Node {
    Node* Left; 
    Node* Right; 
    Node* Up; 
    Node* Down; 
    Node* Column;
    u32   Count; 
    i16   RowID; 
    i16   ColID;
};

struct DLX {
    Node* Rows[MAX_ROWS];
    Node  Columns[MAX_COLS];
    Node  NodeArena[MAX_DLX_NODES];
    i16   Solution[MAX_DOMINOES];
    Node  Head;
    u32   NodesUsedCount;
    u32   SolutionSize;
};

struct Domino { 
    u8 A;
    u8 B; 
};

struct WorkOrder {
    DLX     State;
    Region  Regions[MAX_REGIONS];
    u8      CellToRegion[MAX_CELLS];
    RowMeta MetaData[MAX_ROWS];
    char    Board[MAX_CELLS];
    u8      GridW;
    u8      GridH;
    u16     NumCells;
    u8      NumDominoes;
    Domino  Inventory[MAX_DOMINOES];
};

static inline void 
MemSetZero(void* Dst, u64 N) 
{
    u8* D = (u8*) Dst;

    for (u64 I = 0; I < N; I++) 
        D[I] = 0;
}

static void
AddRow(DLX* Dlx, u16 RowIdx, u16 ColIdx[3]) 
{
    if (Dlx->NodesUsedCount + 3 > MAX_DLX_NODES) 
        return;

    u32 BaseIdx = Dlx->NodesUsedCount;

    Dlx->NodesUsedCount += 3;

    for (u8 K = 0; K < 3; ++K) {
        Node* N = &Dlx->NodeArena[BaseIdx + K];

        N->RowID = RowIdx;
        N->ColID = ColIdx[K];
        N->Column = &Dlx->Columns[ColIdx[K]];
        N->Down = N->Column;
        N->Up = N->Column->Up;
        N->Column->Up->Down = N;
        N->Column->Up = N;
        ++N->Column->Count;
    }

    for (u8 K = 0; K < 3; ++K) {
        Node* H = &Dlx->NodeArena[BaseIdx + K];

        H->Left = &Dlx->NodeArena[BaseIdx + (K + 2) % 3];
        H->Right = &Dlx->NodeArena[BaseIdx + (K + 1) % 3];
    }

    Dlx->Rows[RowIdx] = &Dlx->NodeArena[BaseIdx];
}

static void 
Cover(Node* ColHeader) 
{
    ColHeader->Right->Left = ColHeader->Left;
    ColHeader->Left->Right = ColHeader->Right;

    for (Node* RN = ColHeader->Down; RN != ColHeader; RN = RN->Down) {
        for (Node* HN = RN->Right; HN != RN; HN = HN->Right) {
            HN->Down->Up = HN->Up;
            HN->Up->Down = HN->Down;
            --HN->Column->Count;
        }
    }
}

static void Uncover(Node* ColHeader) 
{
    for (Node* RN = ColHeader->Up; RN != ColHeader; RN = RN->Up) {
        for (Node* HN = RN->Left; HN != RN; HN = HN->Left) {
            HN->Down->Up = HN;
            HN->Up->Down = HN;
            ++HN->Column->Count;
        }
    }

    ColHeader->Right->Left = ColHeader;
    ColHeader->Left->Right = ColHeader;
}

static inline b8
CheckAndApplyCell(Region* R, u8 Val) 
{
    if (R->Type == RULE_EQ && R->FilledCells > 0 && Val != R->FirstVal) 
        return FALSE;

    if (R->Type == RULE_NEQ && (R->Mask & (1 << Val))) 
        return FALSE;

    if (R->Type == RULE_GT) {
        if (R->FilledCells + 1 == R->TotalCells && R->Sum + Val <= R->Target) 
            return FALSE;
    }

    if (R->Type == RULE_LT) {
        if (R->Sum + Val >= R->Target) 
            return FALSE;
    }

    if (R->Type == RULE_SUM) {
        if (R->Sum + Val > R->Target) 
            return FALSE;

        if (R->FilledCells + 1 == R->TotalCells && R->Sum + Val != R->Target) 
            return FALSE;
    }

    if (!R->FilledCells) 
        R->FirstVal = Val;

    R->Mask |= (1 << Val);
    R->Sum += Val;
    ++R->FilledCells;

    return TRUE;
}

static inline void 
UndoCell(Region* R, u8 Val) 
{
    --R->FilledCells;
    R->Sum -= Val;
    R->Mask &= ~(1 << Val);
}

static inline b8 
ApplyDomino(WorkOrder* Order, u16 RowID) 
{
    RowMeta* M = &Order->MetaData[RowID];
    Region* Reg1 = &Order->Regions[Order->CellToRegion[M->R1 * Order->GridW + M->C1]];
    Region* Reg2 = &Order->Regions[Order->CellToRegion[M->R2 * Order->GridW + M->C2]];

    if (!CheckAndApplyCell(Reg1, M->Val1)) 
        return FALSE;

    if (!CheckAndApplyCell(Reg2, M->Val2)) { 
        UndoCell(Reg1, M->Val1); 

        return FALSE; 
    }

    return TRUE;
}

static inline void 
RemoveDomino(WorkOrder* Order, u16 RowID) 
{
    RowMeta* M = &Order->MetaData[RowID];
    Region* Reg2 = &Order->Regions[Order->CellToRegion[M->R2 * Order->GridW + M->C2]];
    Region* Reg1 = &Order->Regions[Order->CellToRegion[M->R1 * Order->GridW + M->C1]];

    UndoCell(Reg2, M->Val2);
    UndoCell(Reg1, M->Val1);
}

static u8 
Solve(DLX* Dlx, WorkOrder* Order) 
{
    if (Dlx->Head.Right == &Dlx->Head) 
        return TRUE;

    Node* ColToCover = Dlx->Head.Right;

    if (ColToCover->Count > 1) {
        for (Node* T = ColToCover->Right; T != &Dlx->Head; T = T->Right) {
            if (T->Count < ColToCover->Count) {
                ColToCover = T;

                if (ColToCover->Count <= 1) 
                    break;
            }
        }
    }

    if (!ColToCover->Count) 
        return FALSE;

    Cover(ColToCover);

    for (Node* RN = ColToCover->Down; RN != ColToCover; RN = RN->Down) {
        if (ApplyDomino(Order, RN->RowID)) {
            Dlx->Solution[Dlx->SolutionSize++] = RN->RowID;

            for (Node* HN = RN->Right; HN != RN; HN = HN->Right) 
                Cover(HN->Column);

            if (Solve(Dlx, Order)) 
                return TRUE;

            --Dlx->SolutionSize;

            for (Node* HN = RN->Left; HN != RN; HN = HN->Left) 
                Uncover(HN->Column);

            RemoveDomino(Order, RN->RowID);
        }
    }

    Uncover(ColToCover);

    return FALSE;
}

static WorkOrder G_Order;

static b8
ParseAndSolve(const char* Line) 
{
    WorkOrder* Order = &G_Order;
    DLX* Dlx = &Order->State;

    MemSetZero(Order->Regions, sizeof(Order->Regions));

    u32 Cursor = 0;

    Order->GridW = 0; Order->GridH = 0;

    while (Line[Cursor] != 'x')
        Order->GridW = Order->GridW * 10 + (Line[Cursor++] - '0');

    ++Cursor;

    while (Line[Cursor] != '|')
        Order->GridH = Order->GridH * 10 + (Line[Cursor++] - '0');

    ++Cursor;
    Order->NumCells = Order->GridW * Order->GridH;
    Order->NumDominoes = 0;

    while (Line[Cursor] != '|') {
        Order->Inventory[Order->NumDominoes].A = Line[Cursor++] - '0';
        Order->Inventory[Order->NumDominoes].B = Line[Cursor++] - '0';
        ++Order->NumDominoes;

        if (Line[Cursor] == ',') 
            ++Cursor;
    }

    ++Cursor;

    for (int I = 0; I < Order->NumCells; ++I) {
        char Ch = Line[Cursor++];

        if (Ch == '0') {
            Order->CellToRegion[I] = 255;
        } else {
            u8 RegID = (Ch >= 'a') ? (Ch - 'a' + 26) : (Ch - 'A');

            Order->CellToRegion[I] = RegID;
            Order->Regions[RegID].TotalCells++;
        }
    }

    ++Cursor;

    while (Line[Cursor] != '\n' && Line[Cursor] != '\r' && Line[Cursor] != '\0') {
        char RegChar = Line[Cursor];
        u8 RegID = (RegChar >= 'a') ? (RegChar - 'a' + 26) : (RegChar - 'A');

        Cursor += 2;

        if (Line[Cursor] == '\0' || Line[Cursor] == '\n' || Line[Cursor] == '\r') {
            Order->Regions[RegID].Type = RULE_NONE; 
            break;
        } else if (Line[Cursor] == '=') {
            Order->Regions[RegID].Type = RULE_EQ; 
            Cursor++;
        } else if (Line[Cursor] == '!' && Line[Cursor+1] == '=') {
            Order->Regions[RegID].Type = RULE_NEQ; 
            Cursor += 2;
        } else if (Line[Cursor] == '>') {
            Order->Regions[RegID].Type = RULE_GT; 
            ++Cursor;
            Order->Regions[RegID].Target = Line[Cursor++] - '0';
        } else if (Line[Cursor] == '<') {
            Order->Regions[RegID].Type = RULE_LT; 
            ++Cursor;
            Order->Regions[RegID].Target = Line[Cursor++] - '0';
        } else if (Line[Cursor] == ' ') {
            Order->Regions[RegID].Type = RULE_NONE; 
            ++Cursor;
        } else {
            Order->Regions[RegID].Type = RULE_SUM;

            u8 Sum = 0;

            while (Line[Cursor] >= '0' && Line[Cursor] <= '9') {
                Sum = Sum * 10 + (Line[Cursor] - '0'); 
                ++Cursor;
            }

            Order->Regions[RegID].Target = Sum;
        }

        if (Line[Cursor] == ',') 
            ++Cursor;
    }

    Dlx->NodesUsedCount = 0; Dlx->SolutionSize = 0;
    Dlx->Head.Left = &Dlx->Head; Dlx->Head.Right = &Dlx->Head;
    Dlx->Head.Up = &Dlx->Head; Dlx->Head.Down = &Dlx->Head;
    Dlx->Head.Column = 0; Dlx->Head.Count = 0;

    u16 TotalCols = Order->NumCells + Order->NumDominoes;

    for (u16 I = 0; I < TotalCols; ++I) {
        Node* Col = &Dlx->Columns[I];

        Col->ColID = I; Col->Up = Col; Col->Down = Col;
        Col->Count = 0; Col->Column = 0;

        b8 IsHole = (I < Order->NumCells) && (Order->CellToRegion[I] == 255);

        if (!IsHole) {
            Col->Left = Dlx->Head.Left; Col->Right = &Dlx->Head;
            Dlx->Head.Left->Right = Col; Dlx->Head.Left = Col;
        }
    }

    u16 CurrentRowID = 0;

    for (u8 D = 0; D < Order->NumDominoes; ++D) {
        u8 PipA = Order->Inventory[D].A;
        u8 PipB = Order->Inventory[D].B;
        u16 DominoColIdx = Order->NumCells + D;

        for (u8 R = 0; R < Order->GridH; ++R) {
            for (u8 C = 0; C < Order->GridW; ++C) {
                if (Order->CellToRegion[R * Order->GridW + C] == 255) 
                    continue;

                if (
                    C + 1 < Order->GridW && 
                    Order->CellToRegion[R * Order->GridW + C + 1] != 255
                ) {
                    Order->MetaData[CurrentRowID] = {R, C, PipA, R, (u8)(C+1), PipB, D};

                    u16 Cols[3] = { 
                        (u16) (R * Order->GridW + C), 
                        (u16) (R * Order->GridW + C + 1), 
                        DominoColIdx 
                    };

                    AddRow(Dlx, CurrentRowID++, Cols);

                    if (PipA != PipB) {
                        Order->MetaData[CurrentRowID] = {
                            R, 
                            C, 
                            PipB, 
                            R, 
                            (u8) (C+1), 
                            PipA, 
                            D
                        };
                        AddRow(Dlx, CurrentRowID++, Cols);
                    }
                }

                if (
                    R + 1 < Order->GridH && 
                    Order->CellToRegion[(R+1) * Order->GridW + C] != 255
                ) {
                    Order->MetaData[CurrentRowID] = {
                        R, 
                        C, 
                        PipA, 
                        (u8) (R+1), 
                        C, 
                        PipB, 
                        D
                    };

                    u16 Cols[3] = { 
                        (u16) (R * Order->GridW + C), 
                        (u16) ((R+1) * Order->GridW + C), 
                        DominoColIdx 
                    };

                    AddRow(Dlx, CurrentRowID++, Cols);

                    if (PipA != PipB) {
                        Order->MetaData[CurrentRowID] = {
                            R, 
                            C, 
                            PipB, 
                            (u8) (R+1), 
                            C, 
                            PipA, 
                            D
                        };
                        AddRow(Dlx, CurrentRowID++, Cols);
                    }
                }
            }
        }
    }

    if (Solve(Dlx, Order)) {
        for (u16 I = 0; I < Order->NumCells; ++I)
            if (Order->CellToRegion[I] == 255) 
                Order->Board[I] = '0';

        for (u32 K = 0; K < Dlx->SolutionSize; ++K) {
            RowMeta* M = &Order->MetaData[Dlx->Solution[K]];

            Order->Board[M->R1 * Order->GridW + M->C1] = M->Val1 + '0';
            Order->Board[M->R2 * Order->GridW + M->C2] = M->Val2 + '0';
        }

        return TRUE;
    }

    return FALSE;
}

static char G_ResultBuf[MAX_CELLS * 2 + 2];
static u8   G_DominoMap[MAX_CELLS];

extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* 
SolvePuzzleStr(const char* PuzzleLine) 
{
    if (!PuzzleLine || !PuzzleLine[0]) 
        return 0;

    if (ParseAndSolve(PuzzleLine)) {
        WorkOrder* Order = &G_Order;
        DLX* Dlx = &Order->State;
        u16 NC = Order->NumCells;

        for (u16 I = 0; I < NC; ++I)
            G_ResultBuf[I] = Order->Board[I];

        G_ResultBuf[NC] = '|';

        MemSetZero(G_DominoMap, sizeof(G_DominoMap));

        for (u32 K = 0; K < Dlx->SolutionSize; ++K) {
            RowMeta* M = &Order->MetaData[Dlx->Solution[K]];
            u8 DID = M->DominoID;
            u8 Ch = DID < 26 
                ? ('A' + DID) 
                : ('a' + DID - 26);

            G_DominoMap[M->R1 * Order->GridW + M->C1] = Ch;
            G_DominoMap[M->R2 * Order->GridW + M->C2] = Ch;
        }

        for (u16 I = 0; I < NC; ++I)
            G_ResultBuf[NC + 1 + I] = G_DominoMap[I] ? G_DominoMap[I] : '.';

        G_ResultBuf[NC * 2 + 1] = '\0';

        return G_ResultBuf;
    }

    return 0;
}

} // extern "C"
