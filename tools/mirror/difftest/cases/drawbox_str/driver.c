/* Driver for the extracted gameTextDrawBox.
 *
 * Plays the caller (gametext.c gameTextRenderStrs), which hands gameTextDrawBox a
 * subtitle STRING through its integer boxId parameter: gameTextDrawBox(NULL, str,
 * slot). Style 3 (subtitle) then null-tests boxId and rebuilds (char*)boxId for the
 * measurer. A second call takes the id path (def != NULL, boxId 0) to show the
 * overload still resolves real text ids.
 *
 * The string comes from mmAlloc: low-4 GB in the oracle (its int boxId round-trip
 * is lossless, as on the real 32-bit build), high-heap malloc in the mirror. Output
 * is the string the measurer received + the derived draw coordinates — data, not
 * addresses. Without the transform the high-heap string pointer truncates in the
 * int parameter and the measurer faults (the negative control). */
#include "shim.h"
#include "snippet.c"
#include <stdio.h>

static void report(const char *label)
{
    printf("%s str=\"%s\" idx=%d id=%d draws=%d sum=%d\n", label,
           gMeasuredStr, gMeasuredIdx, gMeasuredId, gDraws, gDrawSum);
    memset(gMeasuredStr, 0, sizeof gMeasuredStr);
    gMeasuredIdx = gMeasuredId = -1;
    gDraws = gDrawSum = 0;
}

int main(void)
{
    GameTextBox *slot = &gTextBoxes[2];
    char *str = (char *)mmAlloc(32, 0, 0);
    strcpy(str, "Fox! Over here!");

    /* subtitle path: the string travels through boxId */
    slot->style = 3; slot->flags = 0; slot->alpha = 0xC0;
    gameTextDrawBox(NULL, (uintptr_t)str, slot);
    report("subtitle");

    /* id path: def points at a u16 text id; boxId unused */
    u16 *def = (u16 *)mmAlloc(4, 0, 0);
    *def = 0x2A;
    slot->style = 3; slot->flags = 0;
    gameTextDrawBox((struct GameTextDef *)def, 0, slot);
    report("by-id");
    return 0;
}
