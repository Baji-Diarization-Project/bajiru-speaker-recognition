# Bajiru Speaker Recognition
Identify whether regular or low voice Bajiru is speaking


There is a Python part of the project for prototyping and testing, and a C++ part that will be the runtime. [See here for the C++ README](./cpp_impl/README.md)


## Installing the Python development tools

If you already know Python and `uv`, then:

```python

uv sync

```
If that doesn't work out of the box, see [the dependencies](#dependencies) for more info.


Then you can use the entry point `uv run detect_speaker` which will start ingesting microphone audio.


### Scripts

All other scripts can be run via `uv run script_name` and accept a `-h` or `--help` argument

- `quick_vis` - get quick stats & output demo from preset input data, args determine which types of visualization to do
- `vis` - get stats for arbitrary file, can output plots and/or generate demo video

Aforementioned demo video is a small square of color, intended to represent the eye color of the speaker identified. Currently, colors 
are aligned with the *start* of the sound chunk that was processed, rather than afterwards (the latter would be more representative of
processing/chunking latency)


## Dependencies

To run the ML model, you will need a Hugging Face API token, stored in your environment variables under `HUGGING_TOKEN`. Otherwise, a more simple method is the default

- ffmpeg may need to be installed manually on your system
- for Linux and Linux-like environments, you may have issues installing `pyaudio`, and may need to install some combination of: `python3-dev`, `python3-dev`, `libasound-dev`, `portaudio19-dev`, `libportaudio2`, `libportaudiocpp0`


# Development



```python

uv sync  --all-extras

uv run pre-commit install

```

To run the automated tests, use `uv run pytest ./tests` 


This project is set up to use `pylint` for linting,  `ruff` for a formatting \*and\* linting.  All of these can be set up to run in a `pre-commit` hook to maintain quality while developing.

Also includes `mypy` if you hate yourself and/or really like static type checking (like a C++ dev).
