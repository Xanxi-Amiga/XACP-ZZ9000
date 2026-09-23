# Why XACP?

The MNT ZZ9000 is already an excellent RTG graphics card.

For my own Amiga, its original graphics functionality already does most of what I need from an RTG board.

What interested me was something else.

Inside the ZZ9000 is a Xilinx Zynq-7020 containing two ARM Cortex-A9 processors and a large amount of DDR memory.

That is a considerable amount of computing power sitting inside a classic Amiga.

**XACP started from a simple question: can the Amiga use those ARM processors as processors of its own?**

That is the central idea of XACP.

> **XACP is about using the ARM inside the ZZ9000 as a processor for the Amiga.**

Not replacing the Amiga.

Not turning the Amiga into a terminal for another computer.

And not primarily trying to redesign the ZZ9000 as a graphics card.

The Amiga remains the machine running AmigaOS, Workbench and the application environment.

The ARM becomes an additional computing resource available to Amiga software.

---

# Origins of XACP

Work on what would become XACP started during **March and April 2026**, while experimenting with direct use of the ZZ9000 ARM processors from Amiga software.

The initial objective was not to build a new firmware ecosystem.

It was simply to find out whether the processing resources already present on the card could be used as genuine coprocessor resources by a classic Amiga.

This required solving some basic problems first:

* communication between the 68k and ARM;
* shared DDR access;
* cache coherency;
* launching ARM code from AmigaOS;
* returning results to the Amiga;
* and using the framebuffer as a practical output path.

On **7 May 2026**, ARM-generated fractal demos were posted in an **AmiBay development thread**, providing the first public demonstration of the concept that would become XACP.

Those fractal demos demonstrated the basic idea before the later game ports and multimedia applications existed:

```text
Amiga program
     │
     │ sends work / parameters
     ▼
ZZ9000 ARM
     │
     │ performs the computation
     ▼
shared memory / framebuffer
     │
     ▼
Amiga
```

The ARM was no longer only part of the internal implementation of the graphics card.

It could execute computation directly on behalf of Amiga software.

That was the starting point of XACP.

---

# A processor for the Amiga

There are several ways to use the ARM inside a device such as the ZZ9000.

One is to expose individual accelerated services:

```text
decode this image
decompress this buffer
decode this audio stream
process this block of data
```

XACP can provide services like these when they are useful.

But that is not its defining idea.

The more important capability is that the Amiga can give the ARM a complete program to execute.

```text
Amiga
  │
  ├── loads an ARM program
  ├── provides files, input and system integration
  ├── exchanges data through shared DDR
  │
  ▼
ARM Cortex-A9
  │
  ├── runs the engine
  ├── performs the heavy computation
  ├── renders frames
  ├── processes or synthesizes audio
  └── returns results to the Amiga
```

This is how projects such as Doom, Quake, PicoDrive, Rastan and Dark Forces became possible.

The ARM is therefore not merely hidden behind a collection of firmware functions.

For XACP applications, it can behave much more like an additional processor belonging to the Amiga.

---

# The Amiga remains the computer

This distinction is important.

XACP does not try to move the entire Amiga environment onto ARM.

AmigaOS still handles the things that make the machine an Amiga:

```text
Workbench
filesystem
Intuition / GUI
keyboard and mouse
Picasso96 / RTG
AHI
CAMD
application launch and control
```

The ARM handles workloads for which raw processing power is useful:

```text
game engines
emulation
rendering
audio decoding
SoundFont synthesis
video decoding
speech synthesis
image processing
other compute-intensive tasks
```

A typical XACP application therefore spans both processors.

The 68k side remains an Amiga program.

The ARM side provides the processing engine.

---

# Why do this?

Classic Amigas have an established operating system, software libraries, interfaces and hardware ecosystem.

What they do not have is modern amounts of CPU performance.

The ZZ9000 already contains considerably more processing power than a classic 68k processor, so XACP asks:

**why leave that processing power inaccessible to normal Amiga software?**

Using it makes possible software that would otherwise be impractical, or that would require extremely powerful and unusual 68k configurations.

The goal is not simply to produce larger benchmark numbers.

The goal is to make new things possible on a classic Amiga.

---

# From the first fractals to a general coprocessor platform

The public fractal demonstrations posted on AmiBay on **7 May 2026** proved the basic execution model.

From there, XACP developed through increasingly demanding real applications.

The progression was roughly:

```text
March-April 2026
ARM / DDR / 68k development
        ↓
7 May 2026
ARM fractal demos shown publicly
in an AmiBay development thread
        ↓
MP3 / MP2 acceleration
        ↓
MPEG video
        ↓
persistent ARM services
        ↓
general Core1 application loading
        ↓
Doom
        ↓
PicoDrive / Rastan / Quake
        ↓
SoundFont MIDI synthesis
        ↓
larger and more complex application engines
        ↓
general-purpose Amiga/ARM coprocessing
```

Each stage exposed new requirements.

The first fractal demos mainly needed computation and framebuffer access.

Multimedia introduced continuous data streaming.

Game engines required reliable Core1 execution, input, video and audio.

Emulation increased timing and memory requirements.

ZZMIDI introduced a persistent realtime synthesis service on Core0.

Later applications pushed memory management, Core0/Core1 coexistence and the execution of increasingly substantial ARM-side software.

XACP was therefore not designed first and given applications afterwards.

**The applications progressively created XACP.**

---

# Two complementary ways of using the ARM

XACP currently supports two main models.

## Persistent Core0 services

Some workloads make sense as firmware services that remain available continuously.

Examples include:

* MP3 / MP2 decoding;
* ZZMIDI SoundFont synthesis;
* shared multimedia operations.

These behave like hardware-assisted services available to Amiga applications.

## Complete Core1 programs

For larger workloads, XACP can load a complete ARM executable onto Core1.

This is used by projects such as:

* ZZDoom;
* ZZQuake;
* ZZPicoDrive;
* ZZRastan;
* ZZSpeech;
* other complete ARM-side engines and experiments.

This second model is particularly important to XACP.

It means that a developer is not limited to asking the firmware to perform a predefined list of operations.

**An application can bring its own ARM-side engine.**

That makes the ARM a programmable computing resource for the Amiga rather than only an accelerator for functions anticipated by the firmware.

---

# Why would I use XACP?

The simplest answer is:

**because you want to use the ARM processors inside your ZZ9000 to run software for your Amiga.**

The XACP software ecosystem currently covers several areas.

## Games and engines

* **ZZDoom**
* **ZZQuake**
* **ZZRastan**

## Emulation

* **ZZPicoDrive**

## Audio and MIDI

* **ZZMIDI**
* MP3 / MP2 accelerated playback
* AmigaAMP XACP engine
* MPEGA-compatible ARM decoding

## Multimedia

* **ZZ-MPEG**

## Experimental computing

* **ZZSpeech**
* **ZZPPC**
* various ARM and Core1 experiments

## Currently in development

* **ZZDarkForcesNEXT** — a Dark Forces / The Force Engine based XACP port, currently nearing completion
* JPEG and PNG datatype acceleration
* audio mixing services
* networking
* archive decompression
* additional audio, multimedia and application projects

Released software and experimental or development work are intentionally distinguished.

Not every experiment necessarily becomes a public release.

---

# Why not simply use the 68k?

Whenever the 68k can perform a task comfortably, there is little reason to move it elsewhere.

XACP is most useful when the processing requirement is large enough to prevent a classic Amiga from doing something well.

The aim is therefore not to ARM-accelerate everything.

It is to use the ARM selectively where doing so changes what the machine can practically achieve.

A 68030 or 68060 continues to execute AmigaOS and conventional Amiga software.

The Cortex-A9 processors become available when an application needs substantially more computation.

---

# Why XACP has its own firmware

As XACP applications became more ambitious, they required increasingly explicit control over the ARM processors and DDR memory.

Projects needed:

* predictable shared memory;
* ARM-private working memory;
* cache coherency rules;
* Core1 loading and execution;
* framebuffer access;
* audio streaming;
* communication between the 68k, Core0 and Core1;
* reliable application start, stop and recovery.

These requirements gradually became the XACP ABI.

The firmware therefore exists primarily to provide a stable execution environment for XACP software.

It is not intended as a replacement for the original purpose of the ZZ9000.

It extends the card into another role:

> **an ARM coprocessor platform for the Amiga.**

---

# Relationship with the original ZZ9000

XACP builds directly on the work of MNT Research.

The ZZ9000 hardware, RTG architecture and original firmware made all of this experimentation possible.

XACP does not exist because the original ZZ9000 is inadequate as an RTG card.

Quite the opposite.

For my own systems, the original RTG functionality already provides most of the graphics functionality I need.

What interested me was the additional computing hardware already present on the card.

XACP explores how those resources can become part of the Amiga's usable computing environment.

In that sense, XACP is not primarily an attempt to redesign the ZZ9000.

It is an attempt to **use more of the ZZ9000**.

---

# A different emphasis

Other ZZ9000 firmware projects may make different architectural choices.

A service-oriented architecture can expose a broad collection of individual accelerated functions through a firmware API.

XACP also uses firmware services where appropriate, but its emphasis is different.

Its defining capability is that Amiga software can make use of the ARM processors themselves, including running substantial application-specific engines on Core1 while persistent services operate on Core0.

The distinction is essentially:

```text
firmware service model

Amiga application
       │
       ▼
predefined ARM service
       │
       ▼
result
```

compared with the XACP application model:

```text
Amiga application
       │
       ├──── AmigaOS integration
       │
       ▼
application-specific ARM engine
       │
       ├──── computation
       ├──── rendering
       ├──── emulation
       └──── processing
```

Both models can be useful.

XACP concentrates on making the ARM a practical programmable processor for Amiga applications.

---

# Application-driven development

XACP infrastructure normally exists because an application required it.

Doom forced improvements in ARM execution and framebuffer handling.

PicoDrive exercised timing, audio and emulator workloads.

Quake pushed performance and memory requirements considerably further.

ZZMIDI required persistent realtime synthesis on Core0.

More recent applications continue to expose requirements that can then become reusable parts of the platform.

This application-driven development model is deliberate.

A firmware feature is most valuable when it enables useful Amiga software.

---

# Stability and experimentation

XACP began as an experimental project, and experimentation remains an important part of its development.

However, experimental development and stable releases are not the same thing.

The project now aims to maintain clearly defined:

* firmware versions;
* XACP protocol versions;
* shared-memory maps;
* application requirements;
* tested firmware/application combinations;
* source and licensing information.

Experimental projects may require development firmware.

Released applications should target explicitly documented and tested XACP configurations.

The repository changelog contains the detailed technical history.

---

# The long-term goal

XACP is not trying to turn the ZZ9000 into a replacement computer hidden inside the Amiga.

Nor is the goal simply to accumulate firmware functions.

The goal is much simpler:

> **Give classic Amiga software access to the processing power already present inside the ZZ9000.**

The 68k remains the Amiga CPU.

AmigaOS remains the operating system.

The ZZ9000 ARM becomes another processor that the Amiga can put to work.

**That is XACP.**
