# Oracle for the accountant's examples

`acceptance.py` recomputes catalog rollups and accumulation-register balances from the input rows with Python `decimal`. It does not call the engine. The tests under `tests/test_acceptance*.cpp` load the JSON it writes and compare the engine's answers with those figures.

```bash
python3 tools/oracle/acceptance.py
```

That refreshes `tests/fixtures/acceptance/catalogs.json` and `accumulation.json`. Edit the input rows in the script, not the expected numbers in the JSON.
