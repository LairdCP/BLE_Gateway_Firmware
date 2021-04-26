
import logging
import os


def setup(source_script_name: str, console_level=logging.INFO, file_mode='w') -> None:
    """
    Log everything to transcript file that uses top level script name.
    Log console_level and above to console.
    """
    if not os.path.exists("logs"):
        os.makedirs("logs")

    base = os.path.basename(source_script_name)
    transcript_name = "logs/" + base.replace(".py", ".transcript.log")
    # '%(asctime)s : %(name)-16s : %(levelname)-8s : %(message)s'
    logging.basicConfig(format='%(asctime)s : %(message)s',
                        level=logging.DEBUG,
                        filename=transcript_name,
                        filemode=file_mode)
    console = logging.StreamHandler()
    console.setLevel(console_level)
    logging.getLogger('').addHandler(console)


if __name__ == "__main__":
    setup(__file__)
