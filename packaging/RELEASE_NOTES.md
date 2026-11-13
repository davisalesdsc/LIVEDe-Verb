## Aurora DeVerb: de-reverb para instrumentos

Remove a reverberação, o eco e o som de sala de gravações de instrumentos (violão, guitarra, piano, bateria, cordas…) e preserva os ataques das notas.

**Formatos:** AU, VST3 e Standalone. Binário **universal**: roda nativo em Apple Silicon (M1/M2/M3/M4) e em Intel. macOS 10.13 ou mais recente.

### Arquivos
| Arquivo | Para quê |
|---|---|
| `AuroraDeVerb-…-macOS-universal.dmg` | Recomendado: contém o instalador |
| `AuroraDeVerb-…-macOS-universal.pkg` | Instalador direto |
| `AuroraDeVerb-…-bundles.zip` | Plugins soltos, para instalação manual |

### Instalação
1. Abra o `.dmg` e rode **Install Aurora DeVerb.pkg**.
2. Se o macOS bloquear por ser de desenvolvedor não identificado: **Ajustes do Sistema → Privacidade e Segurança → Abrir Mesmo Assim** (ou botão direito → Abrir).
3. Reabra a DAW.

### Validação feita no build
- Testes do motor DSP (44,1 / 48 / 96 kHz): reconstrução perfeita com redução em 0, cauda de reverb −21 a −28 dB, ataques preservados (< 0,2 dB de perda).
- `lipo`: arm64 + x86_64 em todos os bundles.
- `auval -strict` (Apple Silicon) e `pluginval` nível 5 (AU e VST3).
- Teste do instalador `.pkg` num Mac limpo do CI.
