// 캐릭터 모듈 목록.
#include "module_types.inc"
#include "common_module.inc"
#include "kazuya_module.inc"
#include "paul_module.inc"
constexpr std::array<const CharacterModule*, 3> Modules{{&CommonModule, &KazuyaModule, &PaulModule}};
