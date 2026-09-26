<div align="center">

# Iris

**An AI-native programming language built from scratch in C.**

Tensors. Autodiff. Transformer primitives. All built into the language itself.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Language: C99](https://img.shields.io/badge/Language-C99-blue.svg)]()
[![Lines: ~2500](https://img.shields.io/badge/Lines-~2500-green.svg)]()
[![Dependencies: none](https://img.shields.io/badge/Dependencies-none-success.svg)]()

</div>

---

Iris is a small programming language designed for building and training
neural networks. Unlike Python + PyTorch, Iris has **tensors, automatic
differentiation, and neural network operations built directly into the
language itself** — not imported from a library.

It compiles to a single binary. No dependencies beyond libc and libm.

```iris
fn forward(x) {
    let h  = posenc(embedding(x, emb));
    let at = attention(h @ Wq, h @ Wk, h @ Wv);
    let h1 = layernorm(h + at);
    let f  = relu(h1 @ W1);
    return layernorm(h1 + f @ W2) @ Wout;
}

let logits = forward(tokens);
let loss   = cross_entropy(logits, targets);
backward(loss);
adam("Wq", Wq, clip(grad(Wq), 1.0), 0.0005);
