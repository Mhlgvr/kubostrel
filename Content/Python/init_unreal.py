# Runs when the Unreal editor opens this project: makes sure the game's materials exist.
import unreal


def _in_commandlet():
    # The cooker and other commandlets start the editor too; they must not create assets.
    # install_iphone.command creates the materials first with its own -run=pythonscript step.
    try:
        return "-run=" in unreal.SystemLibrary.get_command_line().lower()
    except Exception:
        return False


if not _in_commandlet():
    try:
        import ks_content  # noqa: F401  (importing it creates any missing materials)
    except Exception as error:
        unreal.log_error("[Kubostrel] Material setup failed: %s" % error)
