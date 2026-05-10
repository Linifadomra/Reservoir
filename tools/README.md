## Using the tool
Simply put the UNPOLLUTED (must have NEVER ran this through the blo_editor.py file already) game_data folder in this directory.
From there, run
```bash
python diff.py
```
This will tell you how many differences exist in the source of truth (blo_editor.py)
VS the c++ version. Once it's a perfect match, you will get a message that says
`Congratulations! The files are byte for byte perfect.`
