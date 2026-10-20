# A Última Aula - PS2

Arquivos: `main.c` (seu código), `Makefile`, `.github/workflows/build.yml`.

## Compilar sem instalar nada (pelo celular)
1. Crie um repositório no GitHub e envie estes arquivos (main.c incluído, com a raiz do projeto como pasta principal).
2. Aba **Actions** > "Compilar PS2" > aguarde terminar.
3. Baixe o artefato `ultima_aula` (contém `ultima_aula.elf`).

## Compilar no PC
Instale o ps2dev (https://github.com/ps2dev/ps2dev) e rode `make`.

## Rodar
- Emulador: PCSX2 > Sistema > Iniciar arquivo ELF.
- PS2 real: via uLaunchELF / OPL / pendrive, ou gravar em ISO com o ELF.
