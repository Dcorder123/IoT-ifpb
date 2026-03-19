# Atividade 1 - ESP32 Wi-Fi + SNTP (ESP-IDF)

Repositório: IoT-ifpb (Disciplina optativa do Curso de Engenharia da Computação do IFPB-CG).

Este repositório contém a Atividade 1 com firmware para ESP32 usando:

- Conexão Wi-Fi (modo STA)
- Sincronização de horário via SNTP
- Projeto em ESP-IDF

## Arquivos da atividade

- `CMakeLists.txt`
- `dependencies.lock`
- `main/CMakeLists.txt`
- `main/idf_component.yml`
- `main/simple_connect.c`
- `main/tutorial.c`
- `main/tutorial.h`

## Observação sobre arquivos ignorados

Para facilitar build em outra máquina, arquivos gerados localmente não foram versionados:

- `build/`
- `managed_components/`
- `sdkconfig`
- `sdkconfig.old`

Esses arquivos são regenerados pelo ESP-IDF durante a configuração/compilação.

## Como compilar em outra máquina

1. Instale o ESP-IDF e ative o ambiente.
2. Clone o repositório.
3. Entre na pasta do projeto.
4. Execute:

```bash
idf.py set-target esp32
idf.py build
```

Para gravar na placa:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```
