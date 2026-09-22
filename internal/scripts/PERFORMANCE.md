# Performance gate

Capture identical `RAGEMP_DIAGNOSTICS` sessions before and after protection:
100 synthetic peds, same resolution, 15 minutes after warm-up.

Export profiler scopes as JSON:

```json
{"Aimbot":{"p99Us":120},"FramePedContext":{"p99Us":300},"Esp":{"p99Us":250}}
```

Validate:

```powershell
python scripts/check_performance_budget.py baseline.json candidate.json
```

Budgets:

- Aimbot: at most `+50 µs` p99.
- FramePedContext and ESP: at most `+5 µs` measurement tolerance.
- `audit_hot_paths.py` must also pass; network and VMProtect entries never
  belong in the frame path.
