# Runs when the Unreal editor opens this project: makes sure the game's materials exist.
import unreal


def _skip_setup():
    # The cooker and other commandlets start the editor too; they must not create assets.
    # install_iphone.command creates the materials first with its own -run=pythonscript step.
    # The game itself (-game: play_mac.command and Standalone Game) has no editor to create them with.
    try:
        command_line = unreal.SystemLibrary.get_command_line().lower()
    except Exception:
        return False
    return "-run=" in command_line or "-game" in command_line.replace('"', " ").split()


def _editor_ready():
    try:
        if unreal.get_editor_subsystem(unreal.EditorAssetSubsystem) is None:
            return False
        return not unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets()
    except Exception:
        return False


_tick_handle = None
_waited_seconds = 0.0
_started = False


def _create_materials_when_ready(delta_seconds):
    # This file runs before the editor has finished starting, so the materials are made on a later tick.
    global _waited_seconds, _started
    if _started:
        return
    _waited_seconds += delta_seconds
    ready = _editor_ready()
    if not ready and _waited_seconds < 600.0:
        return
    _started = True
    unreal.unregister_slate_post_tick_callback(_tick_handle)
    if not ready:
        unreal.log_warning("[Kubostrel] The editor did not finish starting, so the materials were not created.")
        return
    try:
        import ks_content  # noqa: F401  (importing it creates any missing materials)
    except Exception as error:
        unreal.log_error("[Kubostrel] Material setup failed: %s" % error)


if not _skip_setup():
    _tick_handle = unreal.register_slate_post_tick_callback(_create_materials_when_ready)
