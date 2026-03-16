# PROJECT NAME
Identify whether regular or low voice Bajiru is speaking


## Installing the tool

If you already know Python and `uv`, then:

```python

uv sync

```

and then you can use the entry point `uv run detect_speaker`:

```

usage: project_name.EXE [-h] 



Project description



options:

-h, --help           show this help message and exit

```

## Dependencies

To run the ML model, you will need a Hugging Face API token, stored in your environment variables under `HUGGING_TOKEN`. Otherwise, a more simple method is the default


# Development



```python

uv sync  --all-extras

uv run pre-commit install

```



This project is set up to use `pylint` for linting,  `ruff` for a formatting \*and\* linting.  All of these can be set up to run in a `pre-commit` hook to maintain quality while developing.

Also includes `mypy` if you hate yourself and/or really like static type checking (like a C++ dev).
