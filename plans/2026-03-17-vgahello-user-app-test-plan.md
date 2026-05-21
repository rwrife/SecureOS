# VGAHello User App Test Plan (2026-03-17)

## Goal
Provide a user-space sample app to validate video text output through the console -> video HAL path in QEMU.

## Scope
- Add `user/apps/vgahello/main.c` sample app.
- Include app in disk-image build mapping so it is present at `/apps/vgahello.bin`.

## Validation Steps
1. Build user app:
- `./build/scripts/build_user_app.sh vgahello`
2. Build disk image:
- `./build/scripts/build_disk_image.sh`
3. Boot console and run:
- `run /apps/vgahello.bin`

## Expected Output
- App prints start/done markers and multiple text-pattern lines.
- Pattern appears in serial console and on VGA/video backend path.
- Wrapping behavior is visually verifiable from long lines.

## Follow-up
- Add dedicated QEMU scripted test profile for `vgahello` output markers.
- Introduce user-space graphics API once pixel/framebuffer syscall surface exists.
