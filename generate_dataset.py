#!/usr/bin/env python3
"""
Esse arquivo foi gerado via Claude Code:
Gera dataset CSV de teste para a B-Tree.

Saida ordenada pela chave primaria (id sequencial 1..N).
Colunas: id, nome, cpf, data_nascimento.
"""

import argparse
import csv
import random
from datetime import date, timedelta

# Configuracao padrao (pode ser sobrescrita por argumento CLI)
DEFAULT_NUM_REGISTROS = 10000
DEFAULT_OUTPUT = "dataset.csv"
DEFAULT_SEED = 42

PRENOMES = [
    "Ana","Antonio" ,"Beatriz", "Camila", "Daniela", "Eduarda", "Fernanda", "Gabriela",
    "Helena", "Isabela", "Julia", "Larissa", "Mariana", "Natalia", "Patricia",
    "Rafaela", "Sofia", "Taina", "Valentina", "Valter",
    "Andre", "Bruno", "Carlos", "Daniel", "Eduardo", "Felipe", "Gabriel",
    "Henrique", "Igor", "Joao", "Lucas", "Marcos", "Nicolas", "Paulo",
    "Rafael", "Samuel", "Tiago", "Thiago", "Vinicius", "William"
]

SOBRENOMES = [
    "Silva", "Santos", "Souza", "Oliveira", "Pereira", "Costa", "Rodrigues",
    "Almeida", "Carvalho", "Lima", "Gomes", "Ribeiro", "Ferreira", "Martins",
    "Araujo", "Nascimento", "Mendes", "Cardoso", "Rocha", "Dias",
]


def gerar_cpf(rng: random.Random) -> str:
    """Gera um CPF valido (com digitos verificadores) formatado XXX.XXX.XXX-XX."""
    base = [rng.randint(0, 9) for _ in range(9)]

    soma = sum(base[i] * (10 - i) for i in range(9))
    d1 = (soma * 10) % 11
    if d1 == 10:
        d1 = 0

    com_d1 = base + [d1]
    soma = sum(com_d1[i] * (11 - i) for i in range(10))
    d2 = (soma * 10) % 11
    if d2 == 10:
        d2 = 0

    digitos = "".join(str(x) for x in base + [d1, d2])
    return f"{digitos[:3]}.{digitos[3:6]}.{digitos[6:9]}-{digitos[9:]}"


def gerar_nome(rng: random.Random) -> str:
    return f"{rng.choice(PRENOMES)} {rng.choice(SOBRENOMES)}"


def gerar_data_nascimento(rng: random.Random,
                          inicio: date = date(1950, 1, 1),
                          fim: date = date(2005, 12, 31)) -> date:
    delta = fim - inicio
    dias = rng.randint(0, delta.days)
    return inicio + timedelta(days=dias)


def gerar_dataset(num_registros: int, output: str, seed: int) -> None:
    rng = random.Random(seed)

    with open(output, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["id", "nome", "cpf", "data_nascimento"])

        for i in range(1, num_registros + 1):
            writer.writerow([
                i,
                gerar_nome(rng),
                gerar_cpf(rng),
                gerar_data_nascimento(rng).isoformat(),
            ])

    print(f"Gerado: {num_registros} registros em '{output}'")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Gera dataset CSV de teste para a B-Tree."
    )
    parser.add_argument(
        "-n", "--num", type=int, default=DEFAULT_NUM_REGISTROS,
        help=f"numero de registros (padrao {DEFAULT_NUM_REGISTROS})",
    )
    parser.add_argument(
        "-o", "--output", default=DEFAULT_OUTPUT,
        help=f"arquivo de saida CSV (padrao {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "-s", "--seed", type=int, default=DEFAULT_SEED,
        help=f"seed do RNG para reprodutibilidade (padrao {DEFAULT_SEED})",
    )
    args = parser.parse_args()

    gerar_dataset(args.num, args.output, args.seed)


if __name__ == "__main__":
    main()
