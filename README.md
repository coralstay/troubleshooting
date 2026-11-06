# troubleshooting

문제를 만나 원인을 끝까지 따라간 기록. 한 건마다 `troubleshooting-N.md` 문서 하나와, 재현 · 검증에 쓴 코드를 담은 같은 이름의 폴더 `troubleshooting-N/`을 둔다.

문서 구조: 요약 → 이상 상황 발견 → 데이터 흐름 → 임시 조치와 실험 → 원인과 증거 → 남은 질문.

| # | 제목 | 날짜 | 코드 |
|---|---|---|---|
| 1 | [Secretive SSH agent: short read drops sign requests ("communication with agent failed")](troubleshooting-1.md) | 2026-11-06 | [`troubleshooting-1/`](troubleshooting-1/) — `secretive_repro.py`(재현 · 검증, C 재조립 relay 내장), `framing_demo.c`(read 한 번 = 메시지 vs 쌓기 · 꺼내기 · 보존) |

코드는 AGPL-3.0-only(`LICENSE`). 문서에 인용한 외부 코드(OpenSSH, Secretive, man-pages)는 각 원 저작자의 것이며 링크로 출처를 밝힌다.
