import os
import logging
import log_wrapper
import sys

FILE_NAME = "./attributes.json"

STRING_TO_FIND = '"x-id":'

BASE = 140
OFFSET = 150


def renumber(argv):
    """
    Read file and re-number all parameters/attributes.
    This should only be used during initial development.
    Once 'released' the x-id cannot change.
    Does not support commands being in the same file.
    """
    logger = logging.getLogger('renumber')
    lst = []
    count = OFFSET
    with open(FILE_NAME, 'r') as fin:
        logger.debug("Opened file " + FILE_NAME)
        for line in fin:
            default_append = True
            if STRING_TO_FIND in line:
                try:
                    x_id, number = line.split()
                    # Leave room for another (possibly incomplete) attribute list
                    number = number.strip(',')
                    if int(number) >= BASE:
                        s = x_id + ' ' + str(count) + ',\n'
                        lst.append(s)
                        default_append = False
                        count += 1
                except:
                    logger.debug(f"Couldn't parse: {line.strip()}")

            if default_append:
                lst.append(line)

    if len(lst) > 0:
        with open(FILE_NAME, 'w') as fout:
            fout.writelines(lst)
            logger.debug("Wrote file " + FILE_NAME)

    logger.info(f'{count - OFFSET} total attributes')


if __name__ == "__main__":
    log_wrapper.setup(__file__, console_level=logging.DEBUG, file_mode='a')
    renumber(sys.argv)
