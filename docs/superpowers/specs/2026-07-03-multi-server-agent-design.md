# Multi-Server Agent Example Design

Date: 2026-07-03
Target: `examples/multi_server_agent`
Status: Draft approved in conversation, pending user review of written spec

## Goal

Replace the current `examples/multi_server_agent` demo with a CLI-based simulated LLM agent that:

- accepts a user task from the command line
- connects to real MCP servers from config
- discovers available MCP tools
- chooses one candidate tool using transparent heuristics
- executes one tool call
- prints both execution logs and an SDK diagnostic report

The example is not meant to be a full agent framework. Its purpose is to exercise the SDK through a realistic tool-discovery and tool-invocation flow so that SDK usability problems become obvious.

## Scope

The first version is intentionally narrow:

- single-turn task execution only
- real MCP servers allowed, with `stdio` as the primary target
- `HTTP/SSE` servers still supported through existing config loading
- one best tool call per run
- no real model inference
- no multi-step planning
- no prompt/router/resource-router main flow
- no interactive REPL

The narrow scope is deliberate. The example should expose friction in the SDK's main agent path rather than hiding it behind extra orchestration.

## Success Criteria

The example is successful if:

1. it can run against a real `stdio` MCP server from config
2. it accepts a task from the command line and runs one full discovery-to-call flow
3. it exits cleanly when no suitable tool is found or arguments cannot be constructed
4. it prints a diagnostic report that identifies at least three concrete SDK usage problems or friction points
5. its tool selection behavior is transparent and reproducible

## Non-Goals

- building a production-quality autonomous agent
- integrating an actual LLM API
- solving arbitrary tool schemas
- supporting multi-turn memory or retry planning
- polishing the best possible UX for end users

## Design Options Considered

### Option 1: Single-file demo

Put all logic into `main.cpp`.

Pros:

- smallest code change
- easiest to run quickly

Cons:

- logic grows into an opaque script
- hard to separate agent flow from SDK diagnostics
- weak testability

### Option 2: Lightweight layered example

Keep the example small, but split responsibilities across a few example-local classes.

Pros:

- preserves example simplicity
- makes the discovery, selection, invocation, and diagnostics stages explicit
- easiest to test and reason about
- best fit for exposing SDK pain points by stage

Cons:

- slightly more file structure than a one-file demo

### Option 3: Harness-style regression tool

Turn the example into a scenario runner with predefined tasks and assertions.

Pros:

- good for future regression coverage
- easier to automate

Cons:

- less representative of real agent usage
- weakens the command-line task input requirement

### Chosen Option

Use Option 2.

This matches the current direction of the repository, which already has higher-level Qt client wrappers such as `McpServerManager` and `McpToolRouter`. The example remains small but no longer collapses all behavior into `main.cpp`.

## File Layout

Keep the example inside `examples/multi_server_agent` and add a few small implementation files:

- `main.cpp`
- `AgentSession.h`
- `AgentSession.cpp`
- `HeuristicToolSelector.h`
- `HeuristicToolSelector.cpp`
- `DiagnosticReporter.h`
- `DiagnosticReporter.cpp`

Update `examples/multi_server_agent/CMakeLists.txt` to build those files into the example executable.

## Responsibilities

### `main.cpp`

Responsibilities:

- parse command-line arguments
- create `QCoreApplication`
- validate required inputs
- construct the session and reporter objects
- trigger execution
- set the process exit code

It should contain almost no business logic.

### `AgentSession`

Responsibilities:

- load config from file
- wait for actual server connection events after config load
- gather connected servers
- trigger tool discovery
- invoke the selector
- build a minimal argument object
- execute one routed tool call
- emit stage-level events to the diagnostic reporter

This class owns the end-to-end flow.

### `HeuristicToolSelector`

Responsibilities:

- score discovered tools against the user task
- explain the score for each candidate
- return the top-ranked tool or a structured "no suitable tool" result

The selector is intentionally simple and deterministic. It should use only explainable signals:

- namespaced tool name
- tool description
- input schema property names
- optional server-name preference from CLI

### `DiagnosticReporter`

Responsibilities:

- collect observations during execution
- group problems by stage
- print a final report with observations, problems, and suggestions

The report is a first-class output of the example, not an afterthought.

## Runtime Interface

Primary invocation:

```powershell
multi_server_agent.exe --config path\to\mcp_servers.json --task "search Qt MCP transport recovery"
```

Optional flags:

- `--timeout-ms 30000`
- `--server <name>`

Rules:

- `--config` is required
- `--task` is required
- no interactive prompt if task is omitted; fail fast with usage text
- `--server` narrows the run to one server to reduce noise

## Runtime Flow

### 1. Read task and config

Parse arguments and validate that task text and config path are present.

Failure here should produce a clean CLI usage error rather than entering the Qt event loop and failing later.

### 2. Load config and connect servers

Call `McpServerManager::loadConfigFile()`.

Important semantic rule:

- `loadConfigFile() == true` means config parsing and client construction started
- it does not mean the target servers are connected and usable

The session must open a bounded waiting window and listen to:

- `clientConnected`
- `clientErrorOccurred`

The session records actual server readiness by event, not by the return value of `loadConfigFile()`.

### 3. Discover tools

For connected servers, gather tool information from:

- `McpToolRouter::exportAllToolsToLlmFormat()`
- underlying cached tool data from the connected clients

This dual view is intentional. It helps reveal:

- cache refresh timing problems
- namespace opacity
- discovery timing mismatches between router and client state

### 4. Select a candidate tool

Run `HeuristicToolSelector` against the discovered tools.

Output should include:

- top three candidates
- each candidate's score
- score reasons
- final selected tool

If there is no suitable tool, the example exits gracefully and the diagnostic report should explicitly classify that result.

### 5. Build minimal arguments

The first version uses conservative argument construction only.

Safe mappings:

- `query`
- `q`
- `keyword`
- `text`
- `prompt`

These fields may receive the raw task text.

If the chosen tool requires fields outside the known safe set and they cannot be inferred reliably, the session must stop before invocation and record a structured diagnostic rather than guessing.

This constraint is deliberate. It puts pressure on the SDK's schema usability and exposes whether the current tool metadata is enough for agent-facing invocation.

### 6. Execute the tool

Invoke through `McpToolRouter::callToolAsync()`.

Record:

- selected namespaced tool name
- routed server name and original tool name
- start time
- end time
- duration
- `isError`
- `errorString`
- whether the returned JSON is easy to render

### 7. Print outputs

Print two explicit sections:

1. `Agent Execution Log`
2. `SDK Diagnostic Report`

The first section shows the flow. The second section explains where the SDK was awkward or insufficient.

## Diagnostic Model

The diagnostic report is grouped by these stages:

- `config/load`
- `server/connect`
- `tool/discovery`
- `tool/selection`
- `tool/call`
- `result/render`

Each stage can emit:

- `observation`
- `problem`
- `suggestion`

The report should prefer concrete, source-level friction over generic complaints.

Examples of intended findings:

- config loading success does not imply server readiness
- router-level tool discovery depends on cache timing that is not obvious to agent authors
- asynchronous connection APIs are hard to observe reliably from examples
- schema-to-arguments bridging is too weak for agent use
- some failures become empty results instead of structured errors
- agent-relevant information is split across router and raw client layers

## Intended SDK Problems To Surface

This example should make the following classes of issues visible during normal use:

### Discovery semantics

- `loadConfig` and `loadConfigFile` are easy to misread as "ready"
- tool availability depends on client cache state and event timing

### Selection ergonomics

- tool metadata may be insufficient for reliable automatic selection
- namespaced tool naming is functional but not especially agent-friendly

### Invocation ergonomics

- argument construction requires too much caller-side guessing
- schema validation and failure feedback may not be strong enough

### Lifecycle visibility

- connection and failure states are not represented by one obvious "ready" abstraction
- example authors must reason across multiple signals and layers

## Error Handling

Failures should be explicit and classified:

- invalid CLI input
- config parse/load failure
- zero connected servers within timeout
- zero discovered tools
- no suitable tool
- insufficient safe arguments
- routed call failure
- malformed or unusable result

The program should not silently continue after those cases. Each one should contribute to the final diagnostic report and produce a non-zero process exit code when appropriate.

## Testing Plan

Add two kinds of tests.

### 1. Selector-focused test

Add a lightweight unit-style test for `HeuristicToolSelector` that verifies:

- stable scoring for a given task and tool set
- expected top candidate ordering
- deterministic tie-breaking

### 2. Session/diagnostic test

Add a Qt-oriented test using `createForTest()` or mock transport patterns already used in the repository to verify:

- no connected server yields a `server/connect` diagnostic
- empty tool discovery yields a `tool/discovery` diagnostic
- missing safe arguments yields a `tool/selection` diagnostic before invocation begins
- invocation failure is reflected in the report rather than swallowed

These tests should verify diagnostic structure, not just that the example executable happens to print something.

## Constraints And Trade-Offs

- Real servers are allowed because the point is to expose real SDK friction.
- `stdio` is prioritized because it is common and stresses startup, logging, timeout, and lifecycle behavior.
- Only one tool call is attempted to keep the example analyzable.
- A deterministic selector is preferred over a smarter but opaque pseudo-agent.

## Open Implementation Notes

- Keep the example self-contained; do not move this logic into the SDK library yet.
- Favor plain Qt/C++17 patterns already used in the repository.
- The selector and reporter should be small enough to understand without reading internal SDK code.
- The diagnostic report should be readable from raw console output without extra tooling.

## User-Instruction Constraint

This spec is written to disk only.

The conversation-level repository rules explicitly forbid staging unstaged changes without clear user authorization. Because of that, this spec must not be staged or committed automatically even though the default brainstorming workflow normally asks for a commit at this stage.
