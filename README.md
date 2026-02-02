# Recall

**Recall** makes debugging faster and clearer by capturing what happened and replaying it later.

## Get Recall

```sh
claude plugin marketplace add WeZZard/skills
claude plugin install wezzard/recall
```

This adds **Recall** skills for capture (`run`), analysis (`analyze`), and system checks (`ada-doctor`)

## Use Recall

**Run**

Run the app explicitly with `/recall:run`

It will build and run your app.

```shell
/recall:run Run the app
```

**Analyze**

Just analyze after the app running done.

After the analysis done it will trigger Claude Code's plan mode to build the fix plan.

```shell
/recall:analyze
```

**Supported Platforms and Programming Languages**

| | macOS | iOS | Android | Linux | Windows |
| --- | --- | --- | --- | --- | --- |
| Swift | ✅ | 📋 | 📋 | 📋 | 📋 |
| Objective-C | ✅ | 📋 | 📋 | 📋 | 📋 |
| C | ✅ | 📋 | 📋 | 📋 | 📋 |
| C++ | ✅ | 📋 | 📋 | 📋 | 📋 |
| Rust | ⚠️ | 📋 | 📋 | 📋 | 📋 |
| Kotlin | 📋 | 📋 | 📋 | 📋 | 📋 |
| Java | 📋 | 📋 | 📋 | 📋 | 📋 |

- ✅: Supported
- ⚠️: Not Tested
- 📋: Planned
- 🚧: Under construction

## Brief Introduction

### Agent-first Debugging Architecture

**Recall** records a layered, shareable timeline of what you saw, what you said, and what your program did.

It’s designed to give AI agents *evidence* (not guesses): a tight, time-aligned bundle they can summarize, search, and cite, including:

- **Screen Recording**: Captures a high-fidelity video of the user's screen.
- **Voice Waveform**: Records the user's voice activity.
- **Function Active Records**: Provides detailed records of function active.

![Explaining Recall](README.md.d/tracks.png "Timeline showing synchronized screen recording, voice waveform, and function trace logs aligned by a red playhead timestamp.")

### Human-first Review Workflow

**Recall** implements a workflow that always puts human review opinions first in defining the problems and reviewing solutions.

It leverages the ask user question tool and plan tool comes with Claude Code to provide **proactive** and **native** review experience.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         ANALYZE WORKFLOW                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌──────────────┐                                                       │
│  │   Voice      │  "It crashed when I clicked save"                     │
│  │  Transcript  │──────────────────────────────┐                        │
│  └──────────────┘                              │                        │
│                                                ▼                        │
│                                    ┌───────────────────┐                │
│                                    │ Intent Extraction │                │
│                                    │    (Step 1)       │                │
│                                    └─────────┬─────────┘                │
│                                              │                          │
│                                              ▼                          │
│                              ┌───────────────────────────┐              │
│                              │   Issue Selection (You)   │◄── Human     │
│                              │        (Step 2)           │    Decision  │
│                              └─────────────┬─────────────┘              │
│                                            │                            │
│         ┌──────────────────────────────────┼────────────────────┐       │
│         │                                  │                    │       │
│         ▼                                  ▼                    ▼       │
│  ┌─────────────┐                   ┌─────────────┐      ┌─────────────┐ │
│  │ Screenshots │                   │   Trace     │      │  Transcript │ │
│  │  @ time T   │                   │   Events    │      │   Context   │ │
│  └──────┬──────┘                   └──────┬──────┘      └──────┬──────┘ │
│         │                                 │                    │        │
│         └─────────────────────────────────┼────────────────────┘        │
│                                           ▼                             │
│                              ┌───────────────────────┐                  │
│                              │  Evidence Correlation │                  │
│                              │       (Step 3)        │                  │
│                              └───────────┬───────────┘                  │
│                                          │                              │
│                                          ▼                              │
│                              ┌───────────────────────┐                  │
│                              │   Hypothesis Review   │◄── Human         │
│                              │     (Steps 4-5)       │    Approval      │
│                              └───────────┬───────────┘                  │
│                                          │                              │
│                                          ▼                              │
│                              ┌───────────────────────┐                  │
│                              │      Fix Plan         │◄── Human         │
│                              │      (Step 6)         │    Sign-off      │
│                              └───────────────────────┘                  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

## Contributing Ideas

Currently ideas are submitted as GitHub issues.

If something feels missing or painful, open an issue with:

- What you were trying to do, what you expected, and what happened instead
- Your platform/version details
- (Optional) A redacted session bundle or screenshots/timestamps

## Contributing Codes

Pull requests are welcome. Please:
- Follow the build/setup guide: [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)
- Keep changes focused and tested
- Run `cargo test --all` before opening a PR

## Contributing Tokens

This project is maintained by AI agents that constantly review the contributed ideas and implement them selectively with AI agents.

If you want to support the project’s LLM-related costs (docs, examples, and evaluations), please open an issue to coordinate or sponsor the project.

**Do not post API keys in issues.**

**Contributable Tokens:**

- Anthropic Claude 4.5 series
- OpenAI GPT 5.2
- Kimi K2.5

## License

MIT

## Support

For issues or questions, please refer to the documentation or create an issue in the repository.

## Quality Metrics

![Quality Gate](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/WeZZard/Recall/main/.github/badges/integration-score.json)
![Rust Coverage](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/WeZZard/Recall/main/.github/badges/rust-coverage.json)
![C/C++ Coverage](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/WeZZard/Recall/main/.github/badges/cpp-coverage.json)
![Build Status](https://github.com/WeZZard/Recall/workflows/Quality%20Gate%20CI/badge.svg)
