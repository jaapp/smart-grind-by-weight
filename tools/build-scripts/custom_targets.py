# tools/custom_targets.py

Import("env")
import os

def upload_ble_action(source, target, env):
    """
    Build firmware and upload over BLE using the Python uploader.
    """
    print("--- Executing custom target: Upload over BLE ---")
    
    # Get the name of the current build environment
    environment = env["PIOENV"]
    project_dir = env["PROJECT_DIR"]

    # First build the firmware
    print("--- Building firmware ---")
    result = env.Execute(f"pio run -e {environment}")
    if result != 0:
        print("❌ Build failed, aborting BLE upload")
        return

    # Get the firmware path
    build_dir = env.subst("$BUILD_DIR")
    firmware_path = os.path.join(build_dir, "firmware.bin")
    
    if not os.path.exists(firmware_path):
        print(f"❌ Firmware not found at {firmware_path}")
        return

    # Run the unified grinder tool for upload
    print("--- Starting BLE upload ---")
    grinder_script = os.path.join(project_dir, "tools", "grinder.py")
    env.Execute(f"python3 {grinder_script} upload {firmware_path}")

# Register custom targets
env.AddCustomTarget(
    name="upload_ble",
    dependencies=None,
    actions=upload_ble_action,
    title="Upload over BLE",
    description="Builds firmware and uploads over BLE"
)