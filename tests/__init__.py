import logging

# Las alarmas provocadas a propósito en los tests no deben ensuciar la salida.
logging.getLogger("cultivo").addHandler(logging.NullHandler())
