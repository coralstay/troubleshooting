---
id: TASK-1
title: 'source-header 적용: troubleshooting (헤더 + 본문 노트 표식)'
status: In Progress
assignee: []
created_date: '2026-11-06T11:57:00Z'
updated_date: '2026-11-06T13:23:00Z'
labels:
  - source-header
  - rollout
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
## 배경
유저 결정(2026-11-06): GitHub에 올라간 저장소 전체에 프론트매터·라이선스·본문 노트 표식을 단다. 규격은 source-header TASK-23(README '본문 노트 표식'). 헤더 적용과 표식을 한 카드에 합친다(커밋은 분리). 유저 결정: 이 저장소에 백로그를 새로 만들어 카드를 둔다.

## 대상 저장소
`~/githubs/troubleshooting` (GitHub coralstay/troubleshooting, private). 소스는 py 1(troubleshooting-1/secretive_repro.py), c 1(troubleshooting-1/framing_demo.c) 두 개뿐이다. .md는 대상이 아니다.

## .source-header.json (초안)
```json
{"include": ["troubleshooting-*/**"], "exclude": ["**/README.md", "backlog/**"]}
```

## 라이선스 조치
LICENSE는 이미 AGPL-3.0. 헤더 license는 AGPL-3.0-only.

## 메모
- 로컬에 troubleshooting-1.md 수정 1개가 커밋되지 않았다(이 카드와 무관). 시작 전에 그 변경을 정리(커밋 또는 되돌림)해야 한다 — 유저 확인.
- 파일이 2개뿐이라 서브에이전트 1명으로 충분하다.
- 선행 완료: backlog 초기화(1c588bb).

## 완료 기준 (승격 후 --ac로 등록)
1. .mailmap을 저장소에 커밋해 authors가 coralstay로 나온다.
2. 라이선스 조치를 마친다(이미 AGPL, 표기 확인).
3. .source-header.json을 확정하고 source-header init으로 헤더를 넣고 description 초안을 쓴다. lint 오류 0.
4. .githooks로 source-header 플러그인을 설치하고 헤더 없는 파일 커밋이 막히는 것을 실측한다.
5. 규격에 따라 표식과 하단 노트를 달고 기존 주석은 변환 규칙으로 옮긴다. 번호 연속·1:1, 주석·헤더 외 코드 변경 없음.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 .mailmap(전역 ~/.mailmap과 같은 매핑)을 저장소에 커밋해 다른 클론·CI에서도 authors가 coralstay로 나온다
- [x] #2 라이선스 조치를 마친다: 이미 AGPL-3.0, 헤더 license는 AGPL-3.0-only로 표기 확인
- [x] #3 .source-header.json을 확정하고(제외 대상 재확인) source-header init으로 헤더를 넣고, 서브에이전트가 description 초안을 쓴다. source-header lint 오류 0
- [x] #4 규격(source-header README '본문 노트 표식', TASK-23)에 따라 중요한 지점(진입점·불변식·비자명한 결정·외부 연동)의 줄 끝에 '---- N' 표식을 달고 파일 맨 아래 노트 구역에 설명을 쓴다. 이미 있는 주석은 변환 규칙(B4)으로 새 포맷에 옮기고 정보를 버리지 않는다. 번호는 파일마다 1부터 연속, 표식:노트 1:1. 주석·헤더 외 코드 줄은 바뀌지 않는다
<!-- AC:END -->

## Definition of Done
<!-- DOD:BEGIN -->
- [x] #1 source-header lint . 오류 0 (출력을 Implementation Notes에 첨부)
- [x] #2 저장소의 기존 테스트·빌드가 헤더 추가 후에도 통과
- [x] #3 유저가 description diff를 검토
- [x] #4 표식 번호가 파일마다 1부터 연속이고 표식:노트가 1:1임을 확인한 출력과, git diff에 주석·헤더 외 변경이 없음을 확인한 결과를 Implementation Notes에 첨부했다
<!-- DOD:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
### Subjective
유저(2026-11-06)가 GitHub에 올라간 저장소 전부의 소스에 프론트매터·라이선스·본문 노트 표식을 달기로 했다. 이 카드는 troubleshooting 한 저장소를 헤더 적용(AC #1~#4)과 표식(AC #5)까지 한 번에 맡는다. 규격은 source-header README '본문 노트 표식'(TASK-23)이다.

### Objective
소스는 py 1(troubleshooting-1/secretive_repro.py)·c 1(troubleshooting-1/framing_demo.c) 두 개뿐. LICENSE는 AGPL-3.0. .mailmap·.githooks 없음. 백로그는 오늘 새로 만들었다(1c588bb). 로컬에 troubleshooting-1.md 수정 1개가 커밋되지 않았다.

### Assessment
미커밋 troubleshooting-1.md는 이 카드와 무관하므로 시작 전에 유저가 정리(커밋 또는 되돌림)한다. 파일이 2개뿐이라 서브에이전트 1명으로 충분하다. 표식은 사람이 읽고 판단해야 해서 토큰이 많이 든다. 헤더 삽입(기계)과 표식(읽기)을 커밋으로 나누고, 표식은 디렉터리 단위 서브에이전트(새 컨텍스트)에 맡긴다.

### Plan
1. 인프라 커밋: .mailmap(전역 ~/.mailmap과 같은 매핑) · LICENSE 조치 확인 · .source-header.json 확정 · .githooks 설치(core.hooksPath) (AC #1 #2 #4).
2. 헤더 삽입 커밋: source-header init → lint 오류 0 (AC #3).
3. description 커밋: 서브에이전트가 초안을 쓰고 유저가 diff를 검토한다 (AC #3, DoD #3).
4. 표식 커밋: 서브에이전트가 파일을 읽고 중요한 지점에만 줄 끝 '---- N'과 하단 노트를 단다. 기존 주석은 B4로 변환한다 (AC #5).
5. 검증: lint --strict, 번호 연속·1:1 확인 스크립트, 기존 테스트·빌드, git diff에 주석·헤더 외 변경 없음.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-06 유저 결정: AC '.githooks 설치(--plugins source-header)'는 후속 draft(source-header DRAFT-7, GF-149 이후)로 분리했다. 이 카드의 AC 번호가 이에 맞춰 당겨졌다(표식 AC 번호 -1).

2026-11-06 구현·검증 기록 (브랜치 task/TASK-1, 커밋 a9a77fd 4a866f0 fa4c1a2)
- AC #1: .mailmap은 전역 ~/.mailmap과 같다(주석뿐, 매핑 없음). git log 작성자는 이미 coralstay.
- AC #2: LICENSE는 AGPL-3.0, README는 AGPL-3.0-only, 두 헤더 license는 AGPL-3.0-only.
- AC #3: .source-header.json은 카드 초안 그대로. 두 소스에 헤더와 한 줄 description. lint --strict . exit 0(직접 실행).
- AC #4: secretive_repro.py 표식 7·노트 7, framing_demo.c 표식 9·노트 9. 번호 1..N 연속, 1:1(직접 확인).
- DoD #2/#4 증거(직접 확인): .py ast.dump가 변경 전과 같다. C 파일은 변경 전후 cc -Wall -Wextra -O2 -c 오브젝트 파일이 cmp로 바이트 동일. 이 저장소에는 별도 테스트·빌드가 없다. framing_demo.c를 별도 위치에 빌드해 실행한 결과가 문서와 맞는다고 서브에이전트가 보고(secretive_repro.py는 실제 Secretive 키가 필요해 실행하지 않았다).
- DoD #3: 유저가 description 286개(8개 저장소) 중 무작위 20개를 샘플 검토하고 '그래도 모두 남겨'라고 결정했다.
- troubleshooting-1.md의 미커밋 수정은 이 카드와 무관하며 건드리지 않았다(여전히 미스테이지).
- AC '.githooks 설치'는 유저 결정으로 후속 draft(source-header DRAFT-7, GF-149 이후)로 분리했다.
<!-- SECTION:NOTES:END -->
