# Runs when the Unreal editor opens this project: makes sure the game's materials exist.
import unreal


def _skip_setup():
    # The cooker and other commandlets start the editor too; they must not create assets.
    # install_iphone.command creates the materials first with its own -run=pythonscript step,
    # and play_mac.command does the same before it starts the game with -game.
    try:
        command_line = unreal.SystemLibrary.get_command_line().lower()
    except Exception:
        return False
    return "-run=" in command_line or "-game" in command_line.replace('"', " ").split()


if not _skip_setup():
    try:
        import ks_content  # noqa: F401  (importing it creates any missing materials)
    except Exception as error:
        unreal.log_error("[Kubostrel] Material setup failed: %s" % error)
