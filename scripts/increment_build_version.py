import os
import logging
import sys

STRING_TO_FIND = "APP_VERSION_PATCH "


def increment_build_version(argv):
    """ Reads header file, increments build version, and writes header file """
    logger = logging.getLogger('increment_build_version')

    if (len(argv)-1) != 1:
        logger.error("File name required")
        return
    else:
        file_name = argv[1]

    lst = []
    with open(file_name, 'r') as fin:
        logger.debug("Opened file " + file_name)
        for line in fin:
            default_append = True
            if STRING_TO_FIND in line:
                logger.debug("Found " + STRING_TO_FIND)
                try:
                    define, name, number = line.split()
                    if name == STRING_TO_FIND.strip():
                        version = (int(number) + 1) % 256
                        s = define + ' ' + STRING_TO_FIND + str(version) + '\n'
                        lst.append(s)
                        logger.debug("New Version string " + s.strip('\n'))
                        default_append = False
                except:
                    logger.debug("Couldn't parse " +
                                 STRING_TO_FIND.strip() + " line")

            if default_append:
                lst.append(line)

    if len(lst) > 0:
        with open(file_name, 'w') as fout:
            fout.writelines(lst)
            logger.debug("Wrote file " + file_name)


if __name__ == "__main__":
    #import log_wrapper
    #log_wrapper.setup(__file__, console_level=logging.DEBUG, file_mode='a')
    increment_build_version(sys.argv)
