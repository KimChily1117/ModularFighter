// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 네이티브 태그 등록.
#include "FighterGameplayTags.h"

namespace FighterGameplayTags
{
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Move_Up, "InputTag.Move.Up", "Up (jump side) direction.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Move_Down, "InputTag.Move.Down", "Down (crouch side) direction.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Move_Back, "InputTag.Move.Back",
                               "Screen-left direction (back for the P1 side).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Move_Forward, "InputTag.Move.Forward",
                               "Screen-right direction (forward for the P1 side).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Button_LP, "InputTag.Button.LP", "Left punch (1).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Button_RP, "InputTag.Button.RP", "Right punch (2).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Button_LK, "InputTag.Button.LK", "Left kick (3).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Button_RK, "InputTag.Button.RK", "Right kick (4).");

UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_UseKeyboard, "InputTag.Training.UseKeyboard",
                               "Control P1 with the keyboard.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_UsePad, "InputTag.Training.UsePad", "Control P1 with the first pad.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_DummyGuard, "InputTag.Training.DummyGuard",
                               "Toggle the dummy's forced guard.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_Diagnostics, "InputTag.Training.Diagnostics",
                               "Toggle the diagnostics HUD.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_ReferencePreview, "InputTag.Training.ReferencePreview",
                               "Toggle the reference motion preview.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_SwapControl, "InputTag.Training.SwapControl",
                               "Switch the controlled fighter.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_DummyAttack, "InputTag.Training.DummyAttack",
                               "Cycle the dummy attack.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_DummyInterval, "InputTag.Training.DummyInterval",
                               "Cycle the dummy attack interval.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_DummyCrouch, "InputTag.Training.DummyCrouch",
                               "Toggle the crouching dummy.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_Reset, "InputTag.Training.Reset", "Reset the training positions.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Training_Pause, "InputTag.Training.Pause", "Pause or resume.");
}
