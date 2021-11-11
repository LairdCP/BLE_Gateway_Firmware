import os
import logging
import sys

MAJOR_STR = "APP_VERSION_MAJOR "
MINOR_STR = "APP_VERSION_MINOR "
BUILD_STR = "APP_VERSION_PATCH "
EXTRACT_FAILED = -1


def extract(line: str, key: str, logger) -> int:
    """
    Extract version number
    """
    result = EXTRACT_FAILED
    if key in line:
        logger.debug("Found " + key)
        try:
            define, name, number = line.split()
            if name == key.strip():
                result = int(number)
        except:
            logger.debug("Couldn't parse " +
                         key.strip() + " line")

    return result


def increment_build_version(argv):
    """
    Reads header file (line-by-line),
    Looks for major, minor, and build version;
    increments build version,
    writes header file,
    writes version to file used by vscode task to assign preprocessor value on command line
    """
    logger = logging.getLogger('increment_build_version')

    if (len(argv)-1) == 0:
        logger.error("source version file name required")
        return
    else:
        file_name = argv[1]

    if (len(argv)-1) == 2:
        output_name = argv[2]
    else:
        output_name = "version.txt"

    major = -1
    minor = -1
    build = -1

    lst = []
    with open(file_name, 'r') as fin:
        logger.debug("Opened file " + file_name)
        # This assumes the file is ordered major, minor, build
        for line in fin:
            lst.append(line)
            major = extract(line, MAJOR_STR, logger)
            if major != EXTRACT_FAILED:
                break

        for line in fin:
            lst.append(line)
            minor = extract(line, MINOR_STR, logger)
            if minor != EXTRACT_FAILED:
                break

        for line in fin:
            default_append = True
            patch = extract(line, BUILD_STR, logger)
            if patch != EXTRACT_FAILED:
                build = (patch + 1) % 256
                s = "#define" + ' ' + BUILD_STR + str(build) + '\n'
                lst.append(s)
                logger.debug("New Version string " + s.strip('\n'))
                default_append = False

            if default_append:
                lst.append(line)

    if len(lst) > 0:
        with open(file_name, 'w') as fout:
            fout.writelines(lst)
            logger.debug("Wrote file " + file_name)

    # A '+' is used as a delimiter between version and build_id by memfault.
    # It isn't added here because version is also used for mcuboot version.
    with open(output_name, 'w') as fout:
        fout.write("version=" + str(major) + "." +
                   str(minor) + "." + str(build))


if __name__ == "__main__":
    #import log_wrapper
    #log_wrapper.setup(__file__, console_level=logging.DEBUG, file_mode='a')
    increment_build_version(sys.argv)
