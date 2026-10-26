# Class Diagrams Without Empty Compartments

Pull request [#8](https://github.com/kineticsystem/raw-converter/pull/8), branch `class-diagram-compact`.

## Goal

The class diagrams of `docs/ARCHITECTURE.md` show no fields, yet every box had an empty fields compartment, and the implementations an empty methods one too. That is space to read past, and it made the diagrams taller.

## Decisions

**`hideEmptyMembersBox` for the implementations.** Their classes have neither fields nor methods, and Mermaid's option draws each as its name alone. The diagram is 556 px tall instead of 705.

**A flowchart for the classes.** Mermaid has no option for a class with methods but no fields: it always draws the empty fields box, in version 12 too. So `classes.mmd` is a flowchart whose boxes look like classes: the stereotype and the name, a line, the methods. The diagram is 888 px tall instead of 1,184, and 710 px wide instead of 770.

**Arrows instead of diamonds.** A flowchart has no aggregation or composition ends. The labels, injected and owns, already say which is which.

## Left Out

The copies of this document in the `modules/raw-converter` of StepIt Stacker and StepIt Grader. They follow when their submodule pointers move.
