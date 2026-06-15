# nodus

A lightweight terminal file manager in plain C — no dependencies.

```bash
make
./build/nodus
```

## Keys

| Key | Action |
|-----|--------|
| `j` / `k` / ↑ / ↓ | Move selection |
| `Enter` | Enter directory / open file |
| `h` / `Backspace` | Go to parent directory |
| `g` / `G` | Jump to top / bottom |
| `/` | Filter by name |
| `.` | Toggle hidden files |
| `n` | Create new directory |
| `r` | Refresh listing |
| `Esc` | Clear active filter |
| `q` | Quit |

Directories are blue, executables green. The status bar shows size and permissions for the selected entry.

```bash
make clean
```
