# PROJECT NAME
Identify whether regular or low voice Bajiru is speaking


## Installing the tool

If you already know Python and `uv`, then:

```python

uv sync

```

and then you can use the entry point `uv run detect_speaker` which will ingest microphone audio.


### Scripts

All other scripts can be run via `uv run script_name` and accept a `-h` or `--help` argument

- `quick_vis` - get quick stats & output demo from preset input data, args determine which types of visualization to do
- `vis` - get stats for arbitrary file, can output plots and/or generate demo video

Aforementioned demo video is a small square of color, intended to represent the eye color of the speaker identified. Currently, colors 
are aligned with the *start* of the sound chunk that was processed, rather than afterwards (the latter would be more representative of
processing/chunking latency)


## Dependencies

To run the ML model, you will need a Hugging Face API token, stored in your environment variables under `HUGGING_TOKEN`. Otherwise, a more simple method is the default


# Development



```python

uv sync  --all-extras

uv run pre-commit install

```



This project is set up to use `pylint` for linting,  `ruff` for a formatting \*and\* linting.  All of these can be set up to run in a `pre-commit` hook to maintain quality while developing.

Also includes `mypy` if you hate yourself and/or really like static type checking (like a C++ dev).