# Lua 설치 매뉴얼

## 목적

학습 커리큘럼을 따라 Lua를 Lua 홈페이지에서 내려받지 않고 `vcpkg`로 설치해서 사용합니다.

현재 `sol2`가 Lua 5.5를 지원하지 않기 때문에 반드시 이 repo의 `vcpkg.json` 기준으로 Lua 5.4.8을 설치해야 합니다.

## 왜 vcpkg로 설치하는가

Lua 공식 홈페이지에서 직접 내려받아 빌드할 수도 있지만, 우리 프로젝트에서는 vcpkg를 사용하는
편이 더 안전합니다.

- 팀원 모두가 같은 Lua 버전을 설치할 수 있습니다.
- 이 repo의 `vcpkg.json`으로 Lua 5.4.8을 고정할 수 있습니다.
- `sol2`와 호환되지 않는 Lua 5.5가 실수로 설치되는 문제를 막을 수 있습니다.
- `lua.hpp`, `lua.lib`, `lua.dll` 위치가 일정하게 유지됩니다.
- Visual Studio 프로젝트가 include/lib/dll 경로를 자동으로 참조할 수 있습니다.
- 각자 수동 빌드 설정을 다르게 해서 생기는 링크 오류를 줄일 수 있습니다.

## 0. 최신 main 받기

Lua 설치 전에 반드시 최신 `main`을 받아야 합니다.

`vcpkg.json`과 Visual Studio 프로젝트 설정이 `main`에 포함되어 있어야 vcpkg 설치와 빌드가 정상 동작합니다.

```powershell
git checkout main
git pull origin main
```

## 1. vcpkg 준비

이미 vcpkg가 설치되어 있으면 이 단계는 생략해도 됩니다.

먼저 vcpkg가 이미 있는지 확인합니다. 아래는 제 컴퓨터 기준 디렉토리입니다.

```powershell
Test-Path C:\krafton-jungle-work\vcpkg\vcpkg.exe
```

결과가 `True`이면 vcpkg가 이미 설치된 상태입니다. 이 경우 바로 `2. 프로젝트 루트로 이동`으로 넘어가면 됩니다.
결과가 `False`이면 아래 명령어로 vcpkg를 설치합니다.

```powershell
git clone https://github.com/microsoft/vcpkg C:\krafton-jungle-work\vcpkg
cd C:\krafton-jungle-work\vcpkg
.\bootstrap-vcpkg.bat
```

## 2. 프로젝트 루트로 이동

각자의 프로젝트 루트로 이동하시면 됩니다.

```powershell
cd C:\krafton-jungle-work\cpp-gametechlab-project-w09
```

## 3. Lua 설치

반드시 프로젝트 루트, 즉 `vcpkg.json`이 있는 위치에서 실행합니다.

```powershell
C:\krafton-jungle-work\vcpkg\vcpkg.exe install --triplet x64-windows
```

설치가 끝나면 아래 파일들이 생겨야 합니다.

```text
vcpkg_installed\x64-windows\include\lua.hpp
vcpkg_installed\x64-windows\debug\lib\lua.lib
vcpkg_installed\x64-windows\debug\bin\lua.dll
```

## 4. 빌드

Visual Studio에서 아래 구성으로 빌드합니다.

```text
Debug | x64
```

빌드하면 `lua.lib`는 자동으로 링크되고, `lua.dll`은 자동으로 아래 위치에 복사됩니다.

```text
NipsEngine\Bin\Debug\lua.dll
```

## 주의사항

- Lua를 수동으로 다운받아서 프로젝트에 넣지 않습니다.
- `vcpkg install lua`만 따로 실행하지 않습니다. 최신 Lua 5.5가 설치되어 sol2와 충돌할 수 있습니다.
- 반드시 repo 루트의 `vcpkg.json`이 있는 위치에서 `vcpkg install --triplet x64-windows`를 실행합니다.
- 빌드는 `x64` 기준입니다. `Win32`는 Lua 런타임 사용 대상이 아닙니다.
