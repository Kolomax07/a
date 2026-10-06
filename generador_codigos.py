#!/usr/bin/env python3
"""Generador de códigos con formato XX-XXXXX-XXXXX-XXXXX.

Uso:
    python3 generador_codigos.py [cantidad] [-o salida.txt]

Los códigos son únicos y usan letras mayúsculas y dígitos.
"""
import argparse
import secrets
import string

ALFABETO = string.ascii_uppercase + string.digits
GRUPOS = (2, 5, 5, 5)  # XX-XXXXX-XXXXX-XXXXX


def generar_codigo() -> str:
    return "-".join(
        "".join(secrets.choice(ALFABETO) for _ in range(n)) for n in GRUPOS
    )


def generar_codigos(cantidad: int) -> list[str]:
    combinaciones = len(ALFABETO) ** sum(GRUPOS)
    if cantidad > combinaciones:
        raise ValueError("Cantidad superior a las combinaciones posibles")
    codigos: set[str] = set()
    while len(codigos) < cantidad:
        codigos.add(generar_codigo())
    return sorted(codigos)


def main() -> None:
    ap = argparse.ArgumentParser(description="Generador de códigos XX-XXXXX-XXXXX-XXXXX")
    ap.add_argument("cantidad", nargs="?", type=int, default=100,
                    help="número de códigos a generar (por defecto 100)")
    ap.add_argument("-o", "--salida", default="codigos.txt",
                    help="archivo de salida (por defecto codigos.txt)")
    args = ap.parse_args()
    if args.cantidad < 1:
        ap.error("la cantidad debe ser mayor que 0")

    codigos = generar_codigos(args.cantidad)
    with open(args.salida, "w", encoding="utf-8") as f:
        f.write("\n".join(codigos) + "\n")
    print(f"{len(codigos)} códigos guardados en {args.salida}")


if __name__ == "__main__":
    main()
