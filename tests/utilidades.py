from datetime import datetime, timedelta

from cultivo.modelos import Lecturas

INICIO = datetime(2026, 10, 3, 0, 0)


def lectura(hora: str = "12:00", dia: int = 0, **valores) -> Lecturas:
    """Lecturas a una hora 'HH:MM' del día INICIO + `dia`."""
    horas, minutos = (int(x) for x in hora.split(":"))
    return Lecturas(ahora=INICIO + timedelta(days=dia, hours=horas, minutes=minutos), **valores)
