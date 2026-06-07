// emcc sudsolve_wasm.cpp -O2 -o sudsolve.js -s EXPORTED_FUNCTIONS="['_SolveSudokuStr','_malloc','_free']" -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']" -s MODULARIZE=1 -s EXPORT_NAME="SudokuModule" -s ALLOW_MEMORY_GROWTH=1 -s TOTAL_STACK=1048576

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

#define SIZE            9
#define BOX_SIZE        3
#define NUM_CELLS       (SIZE * SIZE)
#define MAX_ROWS        (SIZE * SIZE * SIZE)
#define MAX_COLS        (4 * SIZE * SIZE)
#define MAX_DLX_NODES   (MAX_ROWS * 4)
#define UNKNOWN_CELL    '0'

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
    i16   Solution[NUM_CELLS];
    Node  Head;
    u32   NodesUsedCount;
    u32   SolutionSize;
};

struct WorkOrder {
    DLX  State;
    u16  InitialRowHas[SIZE];
    u16  InitialColHas[SIZE];
    u16  InitialBoxHas[SIZE];
    char Board[NUM_CELLS];
};

static inline void 
MemSetZero(void* Dst, u64 N) 
{
    u8* D = (u8*) Dst;

    for (u64 I = 0; I < N; I++) 
        D[I] = 0;
}

static void
AddRow(DLX* Dlx, u16 RowIdx, u16 ColIdx[4]) 
{
    if (Dlx->NodesUsedCount + 4 > MAX_DLX_NODES)
        return;

    u32 BaseIdx = Dlx->NodesUsedCount;

    Dlx->NodesUsedCount += 4;

    for (u8 K = 0; K < 4; ++K) {
        Node* NewNode = &Dlx->NodeArena[BaseIdx + K];

        NewNode->RowID = RowIdx;
        NewNode->ColID = ColIdx[K];
        NewNode->Column = &Dlx->Columns[ColIdx[K]];
        NewNode->Down = NewNode->Column;
        NewNode->Up = NewNode->Column->Up;
        NewNode->Column->Up->Down = NewNode;
        NewNode->Column->Up = NewNode;
        ++NewNode->Column->Count;
    }

    for (u8 K = 0; K < 4; ++K) {
        Node* HNode = &Dlx->NodeArena[BaseIdx + K];

        HNode->Left = &Dlx->NodeArena[BaseIdx + (K + 3) % 4];
        HNode->Right = &Dlx->NodeArena[BaseIdx + (K + 1) % 4];
    }

    Dlx->Rows[RowIdx] = &Dlx->NodeArena[BaseIdx];
}

static void 
Cover(Node* ColHeader) 
{
    ColHeader->Right->Left = ColHeader->Left;
    ColHeader->Left->Right = ColHeader->Right;

    for (Node* RowNode = ColHeader->Down; RowNode != ColHeader; RowNode = RowNode->Down) {
        for (Node* HNode = RowNode->Right; HNode != RowNode; HNode = HNode->Right) {
            HNode->Down->Up = HNode->Up;
            HNode->Up->Down = HNode->Down;
            --HNode->Column->Count;
        }
    }
}

static void 
Uncover(Node* ColHeader) 
{
    for (Node* RowNode = ColHeader->Up; RowNode != ColHeader; RowNode = RowNode->Up) {
        for (Node* HNode = RowNode->Left; HNode != RowNode; HNode = HNode->Left) {
            HNode->Down->Up = HNode;
            HNode->Up->Down = HNode;
            ++HNode->Column->Count;
        }
    }

    ColHeader->Right->Left = ColHeader;
    ColHeader->Left->Right = ColHeader;
}

static inline b8
IsCandidateValid(
    u8 RowIdx, 
    u8 ColIdx, 
    u8 Num, 
    u16* InitialRowHas,
    u16* InitialColHas,
    u16* InitialBoxHas
) {
    u8 BoxIdx = (RowIdx / BOX_SIZE) * BOX_SIZE + (ColIdx / BOX_SIZE);
    u16 Mask = (1 << Num);
    u16 InitRowResult = InitialRowHas[RowIdx] & Mask;
    u16 InitColResult = InitialColHas[ColIdx] & Mask;
    u16 InitBoxResult = InitialBoxHas[BoxIdx] & Mask;

    return (!(InitRowResult || InitColResult || InitBoxResult));
}

static u8 
Solve(DLX* Dlx) 
{
    if (Dlx->Head.Right == &Dlx->Head)
        return TRUE;

    Node* ColToCover = Dlx->Head.Right;

    if (ColToCover->Count > 1) {
        for (Node* TempCol = ColToCover->Right; TempCol != &Dlx->Head; TempCol = TempCol->Right) {
            if (TempCol->Count < ColToCover->Count) {
                ColToCover = TempCol;

                if (ColToCover->Count <= 1)
                    break;
            }
        }
    }

    if (!ColToCover->Count)
        return FALSE;

    Cover(ColToCover);

    for (Node* RowNode = ColToCover->Down; RowNode != ColToCover; RowNode = RowNode->Down) {
        Dlx->Solution[Dlx->SolutionSize++] = RowNode->RowID;

        for (Node* HNode = RowNode->Right; HNode != RowNode; HNode = HNode->Right)
            Cover(HNode->Column);

        if (Solve(Dlx))
            return TRUE;

        --Dlx->SolutionSize;

        for (Node* HNode = RowNode->Left; HNode != RowNode; HNode = HNode->Left)
            Uncover(HNode->Column);
    }

    Uncover(ColToCover);

    return FALSE;
}

static WorkOrder G_Order;

static b8
ParseAndSolve(const char* PuzzleLine) 
{
    WorkOrder* Order = &G_Order;
    DLX* Dlx = &Order->State;

    MemSetZero(Order->InitialRowHas, sizeof(Order->InitialRowHas));
    MemSetZero(Order->InitialColHas, sizeof(Order->InitialColHas));
    MemSetZero(Order->InitialBoxHas, sizeof(Order->InitialBoxHas));

    Dlx->NodesUsedCount = 0;
    Dlx->SolutionSize = 0;
    Dlx->Head.Left = &Dlx->Head;
    Dlx->Head.Right = &Dlx->Head;
    Dlx->Head.Up = &Dlx->Head;
    Dlx->Head.Down = &Dlx->Head;
    Dlx->Head.Column = 0;
    Dlx->Head.Count = 0;
    Dlx->Head.RowID = 0;
    Dlx->Head.ColID = -1;

    MemSetZero(Dlx->Solution, sizeof(Dlx->Solution));
    MemSetZero(Dlx->Rows, sizeof(Dlx->Rows));

    for (u16 I = 0; I < MAX_COLS; ++I) {
        Node* ColNode = &Dlx->Columns[I];

        ColNode->ColID = I;
        ColNode->Up = ColNode;
        ColNode->Down = ColNode;
        ColNode->Count = 0;
        ColNode->Column = 0;
        ColNode->RowID = 0;
        ColNode->Left = Dlx->Head.Left;
        ColNode->Right = &Dlx->Head;
        Dlx->Head.Left->Right = ColNode;
        Dlx->Head.Left = ColNode;
    }

    for (u8 I = 0; I < NUM_CELLS; ++I)
        Order->Board[I] = PuzzleLine[I];

    // NOTE(nathan): Populate bitmasks for given cells and detect conflicts

    for (u8 RInit = 0; RInit < SIZE; ++RInit) {
        for (u8 CInit = 0; CInit < SIZE; ++CInit) {
            if (Order->Board[RInit * SIZE + CInit] != UNKNOWN_CELL) {
                u8 Val = Order->Board[RInit * SIZE + CInit] - '0';
                u8 BoxIdx = (RInit / BOX_SIZE) * BOX_SIZE + (CInit / BOX_SIZE);
                u16 Mask = (1 << Val);
                u16 InitRowResult = (Order->InitialRowHas[RInit] & Mask);
                u16 InitColResult = (Order->InitialColHas[CInit] & Mask);
                u16 InitBoxResult = (Order->InitialBoxHas[BoxIdx] & Mask);

                if (InitRowResult || InitColResult || InitBoxResult)
                    return FALSE;

                Order->InitialRowHas[RInit] |= Mask;
                Order->InitialColHas[CInit] |= Mask;
                Order->InitialBoxHas[BoxIdx] |= Mask;
            }
        }
    }

    // NOTE(nathan): Build DLX rows — unknowns get all valid candidates, 
    //               givens get a single row then pre-cover

    for (u8 R = 0; R < SIZE; ++R) {
        for (u8 C = 0; C < SIZE; ++C) {
            if (Order->Board[R * SIZE + C] == UNKNOWN_CELL) {
                for (u8 Num = 1; Num <= SIZE; Num++) {
                    if (!IsCandidateValid(
                        R, C, Num, 
                        Order->InitialRowHas, 
                        Order->InitialColHas, 
                        Order->InitialBoxHas
                    ))
                        continue;

                    u16 RowIdx = R * SIZE * SIZE + C * SIZE + (Num - 1);
                    u16 BoxIdx = (R / BOX_SIZE) * BOX_SIZE + (C / BOX_SIZE);
                    u16 ColIdx[4] = {
                        (u16) (R * SIZE + C),
                        (u16) (SIZE * SIZE + R * SIZE + (Num - 1)),
                        (u16) (2 * SIZE * SIZE + C * SIZE + (Num - 1)),
                        (u16) (3 * SIZE * SIZE + BoxIdx * SIZE + (Num - 1))
                    };

                    AddRow(Dlx, RowIdx, ColIdx);
                }
            } else {
                u8 Num = Order->Board[R * SIZE + C] - '0';
                u16 RowIdx = R * SIZE * SIZE + C * SIZE + (Num - 1);
                u16 BoxIdx = (R / BOX_SIZE) * BOX_SIZE + (C / BOX_SIZE);
                u16 ColIdx[4] = {
                    (u16) (R * SIZE + C),
                    (u16) (SIZE * SIZE + R * SIZE + (Num - 1)),
                    (u16) (2 * SIZE * SIZE + C * SIZE + (Num - 1)),
                    (u16) (3 * SIZE * SIZE + BoxIdx * SIZE + (Num - 1))
                };

                AddRow(Dlx, RowIdx, ColIdx);

                Node* ChosenRowNode = Dlx->Rows[RowIdx];

                if (!ChosenRowNode)
                    continue;

                Dlx->Solution[Dlx->SolutionSize++] = ChosenRowNode->RowID;

                if (ChosenRowNode->Column->Right->Left == ChosenRowNode->Column)
                    Cover(ChosenRowNode->Column);

                for (Node* NodeInRow = ChosenRowNode->Right; NodeInRow != ChosenRowNode; NodeInRow = NodeInRow->Right)
                    if (NodeInRow->Column->Right->Left == NodeInRow->Column)
                        Cover(NodeInRow->Column);
            }
        }
    }

    if (Solve(Dlx)) {
        for (u32 K = 0; K < Dlx->SolutionSize; ++K) {
            u16 RowID = Dlx->Solution[K];
            u8 Num = (RowID % SIZE) + 1;

            RowID /= SIZE;

            u16 J = RowID % SIZE;

            RowID /= SIZE;

            u16 I = RowID;

            Order->Board[I * SIZE + J] = Num + '0';
        }

        return TRUE;
    }

    return FALSE;
}

static char G_ResultBuf[NUM_CELLS + 1];

extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* 
SolveSudokuStr(const char* PuzzleLine) 
{
    if (!PuzzleLine || !PuzzleLine[0]) 
        return 0;

    if (ParseAndSolve(PuzzleLine)) {
        WorkOrder* Order = &G_Order;

        for (u8 I = 0; I < NUM_CELLS; ++I)
            G_ResultBuf[I] = Order->Board[I];

        G_ResultBuf[NUM_CELLS] = '\0';

        return G_ResultBuf;
    }

    return 0;
}

} // extern "C"
