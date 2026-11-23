# abyssal-claw-invasion
The Abyssal Claw Fleet has invaded the planet ALGEA-CENTAURI:FOUR, your homeworld, are you going to let them just take it!? Drop into the warzone in your Exo-suit and show those snapping scum who's front lawn they stepped onto!!!
## Download & Play (Windows)

1. Go to the **Releases** section on the right side of this repository and open **v0.1.0**.
2. Download **`abyssal-claw-invasion-v0.1.0-win64.zip`** under **Assets** (not the *Source code* archive).
3. Right-click the `.zip` file and choose **Extract All** so all `.dll` files stay in the same folder as `game.exe`[cite: 4, 5].
4. Double-click **`game.exe`** to launch the game[cite: 5]. *(If Windows SmartScreen appears on an unsigned download, click **More info** -> **Run anyway**.)*

## Controls
- **Move:** `WASD` or Arrow Keys
- **Dash:** `Space` or `Left Shift`
- **Attack / Slash:** `J` or Left Click

## Build from Source (Optional — MSYS2 UCRT64)
```bash
g++ -std=c++17 game.cpp -I/ucrt64/include -L/ucrt64/lib -lraylib -lopengl32 -lgdi32 -lwinmm -o game.exe && ./game.exe
```
