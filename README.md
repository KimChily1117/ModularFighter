# ModularFighter

철권 8의 카즈야와 폴을 소재로 만든 UE 5.8 격투 게임 모작입니다. 개인 학습 프로젝트이며 판매하거나 배포하지 않습니다.

전투 판정을 언리얼 밖의 결정적 C++ 코어로 분리했습니다. 언리얼은 입력, 표시, 연출만 맡기 때문에 코어만 되돌려 다시 계산하는 롤백 넷코드가 가능합니다.

## 구성

| 폴더 | 내용 |
|---|---|
| `Source/CombatCore` | C++17 전투 규칙, 상태 저장·복원·해시, 롤백 세션, CPU |
| `Source/InputCore`, `Source/WindowsInput` | 입력 정규화, XInput |
| `Source/NetProtocol` | 매치 서버 통신 메시지 |
| `Source/FighterSimulation` | 코어를 언리얼 모듈로 연결 |
| `Source/FighterGame` | Experience, Game Feature, GAS 미러, 카메라 스택, HUD, 이펙트, 온라인 세션 |
| `Server` | 매칭, 입력 중계, 결과 해시 합의를 맡는 TCP 매치 서버 |

## 참고

- 소스 코드만 공개합니다. 리소스, 원작 기반 데이터, 데이터 도구, 테스트는 포함하지 않아 이 저장소만으로는 빌드되지 않습니다.
- 표시 계층의 일부 구조는 Lyra 샘플을 참고해 이식했으며, 해당 파일 첫 줄에 표시했습니다.
