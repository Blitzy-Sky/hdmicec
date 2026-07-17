# hdmicec Architecture Overview

This document is the conceptual map of the `hdmicec` module: it explains **where the module sits in the RDK-V stack** and **how its parts relate**. `hdmicec` is the <b>CCEC C++ CEC middleware library (`ccec`) plus an OS Abstraction Layer (`osal`)</b>; it acts as the HDMI-CEC HAL <b>"Caller"</b> — it *consumes* a HAL beneath it and serves the Thunder service plugins above it, but it does **not** implement the HAL itself. `Source: hdmicec/rdk_env.xml`.

This overview is the starting point that the two sibling documents drill into: [`hal-interaction.md`](hal-interaction.md) details the two HAL backends, and [`data-flow.md`](data-flow.md) details the message paths and threading.

## Related documents

- [HAL Interaction](hal-interaction.md) — how CCEC talks to **both** HAL paths: the Legacy C HAL and the AIDL/Binder HAL.
- [Data Flow & Concurrency](data-flow.md) — outbound transmit / inbound receive sequences and the `Bus` producer/consumer threading model.

---

## 1. Stack Placement

`hdmicec` occupies the **middleware** tier of the RDK-V HDMI-CEC software stack. Reading the stack top-to-bottom:

1. **Thunder host process** — the WPEFramework/Thunder web-server framework that hosts the CEC service plugins. `Source: README.md`.
2. <b>`entservices-apis` (contracts)</b> — the COM-RPC/JSON-RPC interface contracts the plugins expose to the rest of the system. `Source: README.md`.
3. <b>CCEC middleware (`ccec` + `osal`)</b> — *this module*. `ccec` provides the CEC library (lifecycle, connections, the message pipeline, and the abstract HAL contract), while `osal` provides the OS-abstraction primitives (threads, mutexes, condition variables, queues) that the library's transport is built on. `Source: hdmicec/rdk_env.xml`.
4. __HAL beneath the middleware__ — the __Legacy C HAL__ is the __currently implemented__ backend: an in-process C driver declared in `hdmi_cec_driver.h` (shipped as `libRCECHal.so`). The __AIDL HAL__ is the __migration target__: an out-of-process AIDL/Binder service whose interfaces are declared in the AIDL package `com.rdk.hal.hdmicec` and which registers with the Service Manager under the service name `HdmiCec` (the value of the `IHdmiCec.serviceName` constant). The package name and the service registration name are distinct — see [`hal-interaction.md`](hal-interaction.md). `Source: rdk-halif-hdmi_cec/include/hdmi_cec_driver.h`, `rdk-halif-aidl/hdmicec/current/com/rdk/hal/hdmicec/`.
5. **SoC CEC driver** — the vendor silicon driver that owns the physical CEC line. `Source: rdk-halif-aidl/hdmicec/current/docs/hdmi_cec.md`.

Because CCEC is the HAL **Caller**, both HAL contracts are expressed behind the same `Driver` abstract contract: the middleware always calls `Driver`, and the single concrete `DriverImpl` implements those calls. <b>Today `DriverImpl` is backed by the Legacy C HAL</b> — its source `#include`s the legacy C header `ccec/drivers/hdmi_cec_driver.h`; the AIDL/Binder HAL is the **target** the adapter is being migrated toward, not a second backend selectable at run time. This `Driver`/`DriverImpl` seam is therefore the migration boundary from the legacy C API to AIDL/Binder, and it is examined in depth in [`hal-interaction.md`](hal-interaction.md). The module declares its build-time dependencies as `sdk` and `devicesettings`. `Source: hdmicec/ccec/src/DriverImpl.cpp`, `hdmicec/rdk_env.xml`.

**Diagram 1 — Layered stack placement.**

```mermaid
flowchart TD
    THUNDER["Thunder host process"]
    APIS["entservices-apis contracts"]
    subgraph CCEC["CCEC middleware (hdmicec)"]
        direction TB
        CCEClib["ccec (CEC library)"]
        OSAL["osal (OS abstraction)"]
    end
    LEGACY["Legacy C HAL (current)<br/>libRCECHal.so — in-process C API"]
    AIDL["AIDL HAL (migration target)<br/>package com.rdk.hal.hdmicec, service 'HdmiCec' — out-of-process Binder"]
    SOC["SoC CEC driver"]

    THUNDER --> APIS
    APIS --> CCEC
    CCEC -->|"Driver / DriverImpl seam (implemented today)"| LEGACY
    CCEC -.->|"Driver / DriverImpl seam (migration target)"| AIDL
    LEGACY --> SOC
    AIDL -.-> SOC
```

The two HAL nodes are drawn to distinguish **migration state**, not a run-time choice: the Legacy C HAL (solid arrow) is the backend `DriverImpl` implements today, while the AIDL/Binder HAL (dashed arrow) is the target the seam is being migrated toward. Whichever backend `DriverImpl` is built against, everything above the seam sees only the abstract `Driver` contract.

---

## 2. Component Inventory

The table below lists the key components of the module with their role and source path. Signatures are documented **exactly as they exist** in the current headers (document-as-is).

| Component | Role | Source |
|-----------|------|--------|
| `LibCCEC` | Library **facade** and lifecycle owner, reached through a shared `getInstance()` accessor. The class also declares a **public constructor**, so it follows a *singleton-style* (shared-accessor) pattern rather than enforcing a unique instance. API: `static LibCCEC & getInstance(void)`, `void init(const char * name = 0)`, `void term(void)`, `int getLogicalAddress(int devType)`, `void getPhysicalAddress(unsigned int *physicalAddress)`, `int addLogicalAddress(const LogicalAddress &source)`. | `hdmicec/ccec/include/ccec/LibCCEC.hpp` |
| `Connection` | Application-facing tap into the CEC bus (send/receive). API: `open()`, `close()`, `addFrameListener()` / `removeFrameListener()`, `send(const CECFrame&, int timeout = 0)`, `sendTo(const LogicalAddress&, const CECFrame&, int timeout = 0)`, `sendToAsync()`, `sendAsync()`, `poll()`, `ping()`, `getSource()` / `setSource()`. | `hdmicec/ccec/include/ccec/Connection.hpp` |
| `Bus` | Internal **singleton** producer/consumer transport with inner `Reader` / `Writer` threads. Holds `std::list<FrameListener*> listeners`, `EventQueue<CECFrame*> wQueue`, `Mutex rMutex` / `Mutex wMutex`; exposes `start()` / `stop()`. *(Implementation source — context only.)* | `hdmicec/ccec/src/Bus.hpp` |
| `Driver` | **Abstract** HAL contract accessed via `static Driver & getInstance(void)`. Declares twelve pure-virtual methods: `open`, `close`, `read`, `write`, `writeAsync`, `addLogicalAddress`, `removeLogicalAddress`, `getLogicalAddress`, `getPhysicalAddress`, `isValidLogicalAddress`, `poll`, and `printFrameDetails`. | `hdmicec/ccec/include/ccec/Driver.hpp` |
| `DriverImpl` | The **single concrete adapter** of `Driver` — the migration seam. Provides static HAL callbacks `DriverReceiveCallback` / `DriverTransmitCallback` and an incoming `rQueue` (`typedef EventQueue<CECFrame*> IncomingQueue`). *(Implementation source — context only.)* | `hdmicec/ccec/src/DriverImpl.hpp` |
| `MessageEncoder` | Encodes high-level messages into `CECFrame` bytes via static `encode(...)` overloads. | `hdmicec/ccec/include/ccec/MessageEncoder.hpp` |
| `MessageDecoder` | Decodes a `CECFrame` back into a high-level message via `decode(const CECFrame&)`, dispatching the result through a `MessageProcessor` reference. | `hdmicec/ccec/include/ccec/MessageDecoder.hpp` |
| `MessageProcessor` | Base class with overloaded `process()` methods, one per **supported/decoded** message type — **38** `process()` overloads for the **43** message classes declared in `Messages.hpp`, so **five** message classes have **no** dedicated handler (`GiveAudioStatus`, `ReportArcInitiation`, `ReportArcTermination`, `RequestArcInitiation`, `RequestArcTermination`). Each base-class default logs the frame via `header.print()` / `msg.print()` and otherwise takes no action, so applications **subclass** it to actually handle specific messages. | `hdmicec/ccec/include/ccec/MessageProcessor.hpp` |
| `FrameListener` | Observer interface: `virtual void notify(const CECFrame&) const = 0`. Paired with `FrameFilter::isFiltered(const CECFrame&)` for selective delivery. | `hdmicec/ccec/include/ccec/FrameListener.hpp` |
| `CECFrame` | Raw CEC frame buffer. Declares `enum { MAX_LENGTH = 128 }` and backs it with `uint8_t buf_[MAX_LENGTH]`. | `hdmicec/ccec/include/ccec/CECFrame.hpp` |
| OSAL primitives | `Thread`, `Mutex` / `AutoLock`, `ConditionVariable`, `EventQueue`, `Runnable`, `Stoppable` — the OS-abstraction building blocks used by `Bus` for its producer/consumer model. | `hdmicec/osal/include/osal/` |

> **Note on frame capacity.** `CECFrame` reserves `MAX_LENGTH = 128` bytes of buffer in code. `Source: hdmicec/ccec/include/ccec/CECFrame.hpp`. Real CEC messages are far smaller: the AIDL HAL contract documents the maximum message size — header block plus opcode block plus operand blocks — as `16 * 8` bits (16 bytes). `Source: rdk-halif-aidl/hdmicec/current/com/rdk/hal/hdmicec/IHdmiCecController.aidl:95`. The 128-byte buffer is therefore a generous code-level constant, not the protocol's own message-size limit.

<hr>

## 3. Class Relationships

The middleware wires its components into a single downward call chain from the facade to the HAL, with the message pipeline and the observer contract attached at the application-facing edge:

- <b>`LibCCEC`</b> owns the module lifecycle (`init` / `term`) and answers address queries (`getLogicalAddress`, `getPhysicalAddress`, `addLogicalAddress`). It drives **both** the HAL and the transport directly through their shared accessors: `init()` **opens the HAL and then starts the transport** — `Driver::getInstance().open()` followed by `Bus::getInstance().start()` — while `term()` tears them down in the **reverse order** — `Bus::getInstance().stop()` followed by `Driver::getInstance().close()`. `LibCCEC` therefore has a <b>direct lifecycle-control edge to `Bus`</b> (not only to `Driver`). **Note the nested, duplicated HAL calls:** `Bus::start()` *also* calls `Driver::getInstance().open()` and `Bus::stop()` *also* calls `Driver::getInstance().close()`, so `init()` issues **two** `Driver::open()` calls (direct, then again via `Bus::start()`) and `term()` issues **two** `Driver::close()` calls (via `Bus::stop()`, then again directly). The current `DriverImpl` is **idempotent** for these — `open()` returns early unless the state is `CLOSED`, and `close()` returns early unless the state is `OPENED` — so only the first open and the first close perform real HAL work and each duplicate call is a **no-op**. `Source: hdmicec/ccec/include/ccec/LibCCEC.hpp`, `hdmicec/ccec/src/LibCCEC.cpp`, `hdmicec/ccec/src/Bus.cpp`, `hdmicec/ccec/src/DriverImpl.cpp`.
- <b>`Connection`</b> is the application's handle onto the CEC bus. It holds a reference to the `Bus` singleton (`Bus &bus`) and registers a private `DefaultFrameListener` (guarded by a private `DefaultFilter`) so that received frames are routed back to the application. `Source: hdmicec/ccec/include/ccec/Connection.hpp`.
- <b>`Bus`</b> (singleton) is the actual transport. It calls `Driver::getInstance()` to perform the real `read()` / `write()`, keeps the registered `listeners`, buffers outbound frames in `EventQueue<CECFrame*> wQueue`, and runs its inner `Reader` and `Writer` — each declared `: public Runnable, public Stoppable`. `Source: hdmicec/ccec/src/Bus.hpp`.
- <b>`Driver`</b> is abstract; <b>`DriverImpl`</b> is its single concrete subclass and the seam to the HAL, exposing the static `DriverReceiveCallback` / `DriverTransmitCallback` entry points and buffering inbound frames in its `rQueue`. `Source: hdmicec/ccec/include/ccec/Driver.hpp`, `hdmicec/ccec/src/DriverImpl.hpp`.
- <b>`MessageEncoder`</b>, <b>`MessageDecoder`</b>, and <b>`MessageProcessor`</b> are *application-side utilities*; they are **not** dependencies of `Connection`, whose header includes the project headers `osal/Mutex`, `CCEC`, `FrameListener`, `Operands`, `Driver`, `LibCCEC`, and `Exception` — but **not** the message-pipeline helpers. Application code uses the encoder to turn a high-level message into a `CECFrame` before sending, and pairs the decoder with a `MessageProcessor` to reconstruct and dispatch a received `CECFrame`: `MessageDecoder` holds a `MessageProcessor &` and invokes the matching `process()` overload, whose base-class default logs the frame (via `header.print()` / `msg.print()`) unless the application subclasses `MessageProcessor` to handle it. <b>`FrameListener`</b> is the observer contract over which received frames are delivered from the `Bus`. `Source: hdmicec/ccec/include/ccec/Connection.hpp`, `hdmicec/ccec/include/ccec/MessageEncoder.hpp`, `hdmicec/ccec/include/ccec/MessageDecoder.hpp`, `hdmicec/ccec/include/ccec/MessageProcessor.hpp`, `hdmicec/ccec/include/ccec/FrameListener.hpp`.

**Diagram 2 — Component / class relationships.** The diagram is anchored on the real `Driver` (abstract) → `DriverImpl` (concrete) inheritance seam.

```mermaid
classDiagram
    class LibCCEC {
        <<shared accessor>>
        +getInstance()
        +init()
        +term()
        +getLogicalAddress()
        +getPhysicalAddress()
        +addLogicalAddress()
    }
    class Connection {
        +open()
        +close()
        +send()
        +sendTo()
        +sendAsync()
        +sendToAsync()
        +poll()
        +ping()
        +addFrameListener()
        +removeFrameListener()
    }
    class Bus {
        <<singleton>>
        +getInstance()
        +start()
        +stop()
        +addFrameListener()
        +removeFrameListener()
    }
    class Driver {
        <<abstract>>
        +getInstance()
        +open()
        +close()
        +read()
        +write()
        +writeAsync()
        +addLogicalAddress()
        +removeLogicalAddress()
        +getLogicalAddress()
        +getPhysicalAddress()
        +isValidLogicalAddress()
        +poll()
        +printFrameDetails()
    }
    class DriverImpl {
        +DriverReceiveCallback()
        +DriverTransmitCallback()
    }
    class MessageEncoder {
        +encode()
    }
    class MessageDecoder {
        +decode()
    }
    class MessageProcessor {
        +process()
    }
    class FrameListener {
        <<interface>>
        +notify()
    }
    class CECFrame {
        +MAX_LENGTH
    }

    LibCCEC --> Driver : opens / closes (lifecycle)
    LibCCEC --> Bus : starts / stops (lifecycle)
    Connection --> Bus : holds reference
    Connection ..> Driver : isValidLogicalAddress()
    Connection ..> FrameListener : registers
    Bus --> Driver : reads and writes
    Bus o-- FrameListener : notifies
    Bus ..> CECFrame : queues
    Driver <|-- DriverImpl : realizes
    MessageEncoder ..> CECFrame : encodes to
    MessageDecoder ..> CECFrame : decodes from
    MessageDecoder --> MessageProcessor : dispatches to
```

---

## 4. Responsibility Split (Middleware vs HAL)

The boundary between `hdmicec` and the HAL beneath it follows the RDK HDMI-CEC contract precisely:

- **CCEC middleware owns the CEC *high-level* protocol** — device discovery, logical-address allocation, and message semantics (encoding, decoding, and dispatch through the `Message*` pipeline). `Source: rdk-halif-aidl/hdmicec/current/docs/hdmi_cec.md`.
- **The HAL owns the CEC *low-level* protocol** — electrical timing, bus arbitration, retries, and ACK sampling — as defined by HDMI-CEC 1.4b. `Source: rdk-halif-aidl/hdmicec/current/docs/hdmi_cec.md`.

Crucially, the HAL treats frames as **opaque**: the caller (the middleware) must pass **fully-formed** message frames — a header block and its data blocks — and the HAL neither parses nor interprets their command-level meaning. All opcode/operand parsing and semantics therefore live in the middleware. `Source: rdk-halif-aidl/hdmicec/current/docs/hdmi_cec.md`. This clean split is what <b>confines the HAL migration to the `DriverImpl` adapter</b>: code *above* the `Driver`/`DriverImpl` seam is insulated from the change, while `DriverImpl` itself must be re-implemented to satisfy the differing AIDL contract. The two backends and the specific adaptations required are compared side-by-side in [`hal-interaction.md`](hal-interaction.md).

---

## 5. Design Patterns

The module's structure is best understood through five recurring patterns, each tied to a concrete component:

- **Layered architecture** — CCEC is a well-defined middleware layer between the Thunder service plugins above and the HAL below, communicating with each through a narrow, explicit contract. `Source: hdmicec/rdk_env.xml`.
- **Adapter / migration seam** — the abstract `Driver` contract plus the concrete `DriverImpl` adapter isolate the code above the seam from any specific HAL implementation. `DriverImpl` currently adapts the **Legacy C HAL**; the same seam is where the **AIDL/Binder** target will be adopted, letting the Legacy-C-to-AIDL migration proceed behind the stable `Driver` interface. `Source: hdmicec/ccec/include/ccec/Driver.hpp`, `hdmicec/ccec/src/DriverImpl.hpp`, `hdmicec/ccec/src/DriverImpl.cpp`.
- **Producer/consumer** — the `Bus` runs inner `Reader` and `Writer` threads over an `EventQueue<CECFrame*> wQueue`, decoupling the callers that enqueue frames from the thread that drains the queue to the HAL. `Source: hdmicec/ccec/src/Bus.hpp`.
- **Observer** — inbound frames are delivered through the `FrameListener` interface (`notify(const CECFrame&)`), letting multiple application listeners subscribe without the transport knowing their concrete types. `Source: hdmicec/ccec/include/ccec/FrameListener.hpp`.
- **Singleton / shared accessor** — the module's shared, process-wide services — `LibCCEC`, `Bus`, and `Driver` — are each reached through a `getInstance()` accessor. `Bus` enforces uniqueness with a **private constructor** (a true singleton), whereas `LibCCEC` and `Driver` declare **public constructors**, so their `getInstance()` provides a shared, conventional access point without preventing additional instances from being constructed. `Source: hdmicec/ccec/include/ccec/LibCCEC.hpp`, `hdmicec/ccec/src/Bus.hpp`, `hdmicec/ccec/include/ccec/Driver.hpp`.

---

*For the two HAL backends and the migration seam in detail, continue to [HAL Interaction](hal-interaction.md); for the transmit/receive sequences and the `Bus` threading model, see [Data Flow & Concurrency](data-flow.md).*
