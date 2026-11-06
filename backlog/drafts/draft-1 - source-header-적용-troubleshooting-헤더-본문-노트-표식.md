---
id: DRAFT-1
title: 'source-header 적용: troubleshooting (헤더 + 본문 노트 표식)'
status: Draft
assignee: []
created_date: '2026-11-06T11:57:00Z'
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
