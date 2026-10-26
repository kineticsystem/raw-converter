# Diagrams as SVG Images, from Mermaid Sources

Pull request [#7](https://github.com/kineticsystem/raw-converter/pull/7), branch `mermaid-diagrams`.

## Goal

Every diagram of `docs/ARCHITECTURE.md` in one style, the one of the `beautiful-mermaid-diagrams` skill: colours by role, compact, readable in light and dark mode. StepIt Macro and its modules made the same change, in [stepit-macro#103](https://github.com/kineticsystem/stepit-macro/pull/103).

## Decisions

**An SVG image, not a Mermaid block.** GitHub stretches a Mermaid block to the width of the page, so a narrow diagram becomes very tall. An image shows at the width we give it. The source stays beside it, in `docs/diagrams/<name>.mmd`.

**A hash, not a render, in CI.** `bin/render-diagrams.sh` writes the SHA-256 of the source into the first line of the SVG. `bin/check-diagrams.sh` compares the two, so CI needs neither Node.js nor Chrome. Whoever changes a `.mmd` renders it and commits both.

**In the format job.** `ci-format.yml` looks at the whole repository, so it runs the check; `bin/test.sh` runs it last.

**Two class diagrams, not one.** The classes were 2,000 px wide. `Converter` with its interfaces is one diagram now, and the interfaces with their implementations the other, each read from left to right.

**The pipeline in three zones**: read, repair and write, around `RawImage`, the hero of the diagram.

## Left Out

The copies of this repository under StepIt Stacker and StepIt Grader, `modules/raw-converter`: they move with an update of their submodule pointers.
