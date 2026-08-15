# Contribution to BlaeckSerial

First, thank you for taking the time to contribute to this project.

You can submit changes via GitHub Pull Requests.

Please:

1. Document every public name you add, and give it an example — the rules are in
   [extras/API-STYLE.md](extras/API-STYLE.md), and CI checks them
2. Call the object `Blaeck` in examples and doc comments, never `BlaeckSerial` — a
   variable sharing its type's name switches off autocomplete in VS Code
3. Say so in the pull request if a change touches the public API or the frames on
   the wire, since [BlaeckTCP](https://github.com/sebaJoSt/BlaeckTCP) usually needs
   the same change
