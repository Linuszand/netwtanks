# Network Tank Arena
## Summary
### Interpolation
Since I got annoyed that the clients tank was stuttering/jittering a lot, I decided to implement interpolation to smooth out the movement of the cliens tank.

## How to Run
### Via Github
1. Clone from github using whatever method you want.
2. Start the project using Unreal/Epic Games Launcher or start via IDE.
### Via the project I sent from Omniway
1. Start the project using Unreal/Epic Games Launcher or start via IDE.

## How to Test
1. Click on the three vertical dots next to the STOP button.
2. Under multiplayer options, change the netmode to listen server.
3. Also, just above netmode, change to amount of players to 2 instead of one.
4. Under modes, click on New Editor Window (PIE).
5. Click on either of the windows to control the tank you want to control and test if the interpolation is working.
6. If you want to test impaired network conditions, go to Editor Preferences.
7. Click on Play under the LEVEL EDITOR text.
8. Scroll down to Enable Nework Emulation and then play around with the settings.
9. Done.

## What I Changed/Improved/Implemented
- `ServerSnapshot` is a `FTankSnapshot` struct that is used to send data from the server to the client. It's in the `TankSnapshot.h` file.
- I `SetReplicateMovement(false)` and unchecked the Replicate Movement box on the Blueprint (BP_Tank).
- Added `OnRep_ServerSnapshot` buffering to timestamp incoming snapshots, so the client has buffered data to interpolate across.
- In the Tick method inside TankPawn.cpp, I added `ROLE_SimulatedProxy` snapshot interpolation for the client side using `RenderTime`, with buffer pruning (removing old & already used snapshots, so that the array `SnapshotBuffer` does not grow indefinitely/infinitely in memory `SnapshotBuffer.RemoveAt(0);`.
