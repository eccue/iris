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

## Install

**No dependencies. One command.**

```bash
cc -std=c99 -O2 -o iris iris.c -lm
```

Then run:

```bash
./iris              # interactive REPL
./iris script.ir    # run a script
```

### Android / Termux

```bash
pkg install clang
clang -std=c99 -O2 -o $PREFIX/bin/iris iris.c -lm
chmod +x $PREFIX/bin/iris
iris
```

Now `iris` works from anywhere in Termux.

### Test it

```
iris> let x = 2 + 3 * 4;
iris> print x;
14
iris> print sqrt(144);
12
iris> exit
```

### Train a model

```bash
curl -o corpus.txt https://www.gutenberg.org/files/11/11-0.txt
iris irispoet.ir
```

That trains a small character-level GPT on Alice in Wonderland. Takes
about 5–10 minutes on a phone, saves weights to `.bin` files, and prints
generated text at the end.

---

## What is Iris?

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
```

That is a complete transformer block — forward pass, backward pass, and
weight update — written entirely in Iris.

---

## Table of Contents

- [Install](#install)
- [What is Iris?](#what-is-iris)
- [Features](#features)
- [Language Guide](#language-guide)
- [Train a Tiny GPT](#train-a-tiny-gpt)
- [Builtin Reference](#builtin-reference)
- [How It Works](#how-it-works)
- [Project Structure](#project-structure)
- [What It Is and What It Isn't](#what-it-is-and-what-it-isnt)
- [Contributing](#contributing)
- [License](#license)

---

## Features

| | |
|---|---|
| **Native tensors** | `@` for matrix multiplication, `+ - * /` for elementwise ops |
| **Reverse-mode autodiff** | `backward(loss)` and `grad(W)` — same idea as PyTorch |
| **Neural network primitives** | `attention`, `layernorm`, `gelu`, `softmax`, `embedding`, `dropout` |
| **Optimizers** | `adam` with bias correction, `sgd` with momentum support |
| **Gradient clipping** | `clip(grad(W), 1.0)` prevents training explosions |
| **Garbage collection** | Mark-and-sweep from the environment chain — no leaks |
| **Save / load** | Tensors serialize to `.bin` files with a magic header |
| **Byte-level tokenizer** | `tokenize` / `detokenize` — 256-token vocab |
| **Kahan summation** | Matmul accumulation uses compensated summation for stability |
| **Tiled matmul** | 32×32 cache-blocked for performance on CPU |
| **Zero dependencies** | Only `libc` and `libm` |

---

## Language Guide

### Variables and Types

```iris
let n     = 42;         # number (double)
let s     = "hello";    # string
let b     = true;       # boolean
let t     = zeros(3,4); # tensor (3 rows, 4 cols)
```

Iris is dynamically typed. Variables are declared with `let` and reassigned
with `=`.

### Operators

```iris
let a = 2 + 3 * 4;         # arithmetic: + - * / %
let c = (a > 10) and (b < 20);
let is_equal = (a == 14);
```

| Operators | Meaning |
|---|---|
| `+ - * / %` | Arithmetic |
| `== != < > <= >=` | Comparison |
| `and` `or` `not` | Logical |
| `@` | Matrix multiplication |

### Control Flow

```iris
if (x > 0) {
    print "positive";
} else {
    print "not positive";
}

while (i < 10) {
    i = i + 1;
}

for k in range(5) {
    print "iteration " + str(k);
}
```

### Functions

```iris
fn add(a, b) {
    return a + b;
}

fn factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}
```

Functions are first-class. They can be passed around, returned from other
functions, and capture their surrounding environment (closures).

### Tensors

```iris
let A = randn(3, 3);     # 3×3 random normal
let B = ones(3, 3);      # 3×3 filled with 1.0
let z = zeros(2, 5);     # 2×5 filled with 0.0
let r = arange(10);      # [0, 1, 2, ..., 9]

let C = A @ B;           # matrix multiply (3×3) @ (3×3)
let D = A + B;           # elementwise add
let E = A * 2.0;         # scalar multiply

print shape(C);          # prints [3, 3]
print C;                 # prints tensor contents
```

### Neural Network Operations

```iris
let h  = relu(x);            # ReLU activation
let s  = sigmoid(x);         # Sigmoid
let g  = gelu(x);            # GELU (transformer standard)
let sm = softmax(x);         # Row-wise softmax
let ln = layernorm(x);       # Layer normalization
let at = attention(Q, K, V); # Scaled dot-product attention
let em = embedding(ids, W);  # Row lookup from embedding table
let pe = posenc(x);          # Sinusoidal positional encoding
let dp = dropout(x, 0.1);    # Dropout (disabled in eval mode)
let cc = concat(a, b);       # Concatenate along columns
```

### Automatic Differentiation

```iris
reset();                          # clear the autodiff tape
let y = model(x);                 # forward pass
let loss = mse(y, target);        # compute loss
backward(loss);                   # reverse-mode pass

let gW = grad(W);                 # gradient of W
let gB = grad(b);                 # gradient of b
```

`backward()` walks the computation graph in reverse, applying the chain
rule at each step. `grad(W)` returns a new tensor of the same shape,
containing the derivative of the loss with respect to each element of `W`.

### Optimizers

```iris
adam("W", W, clip(grad(W), 1.0), 0.001);   # Adam with clipping
sgd(W, grad(W), 0.01);                     # plain SGD
```

`adam` maintains per-parameter state (momentum and variance) across calls.
The first argument is a name string used as the key for that state.

### Save and Load

```iris
save(W, "weights.bin");
let W2 = load("weights.bin");
```

The format is a 6-byte magic header, followed by shape info and raw doubles.

---

## Train a Tiny GPT

Iris ships with `irispoet.ir` — a complete character-level transformer you
can train on any text file.

**1. Put some text in `corpus.txt`:**

```bash
curl -o corpus.txt https://www.gutenberg.org/files/11/11-0.txt
```

**2. Run the training script:**

```bash
./iris irispoet.ir
```

**3. Watch the loss drop:**

```
corpus: 151191 chars

training 8000 steps (LR 0.0007, D 48, dropout 0.1)...

step 0     loss 5.48718
step 200   loss 3.81204   ########
step 400   loss 3.15491   #############
step 1000  loss 2.28417   #################
step 2000  loss 1.62104   ####################
step 4000  loss 1.18529   #######################
step 6000  loss 0.98107   ##########################
step 7999  loss 0.87134   #############################

done in 412s
saved 14 weights.
```

**4. See the generated text:**

```
════════════════════════════════════════════
  Model continues these openings:
════════════════════════════════════════════

[Alice was]
Alice was beginning to get very tired of sitting by her sister on the
bank, and of having nothing to do: once or twice she had peeped into
the book her sister was reading, but it had no pictures or conversations
```

The model is small (~55,000 parameters), so its output is imperfect. But
it learns real grammar, real vocabulary, and real dialogue structure. It
runs entirely on CPU.

### Tuning Knobs

Edit the top of `irispoet.ir`:

```iris
let D     = 48;      # model dimension (bigger = smarter, slower)
let CTX   = 24;      # context length (how many chars it sees at once)
let LR    = 0.0007;  # learning rate (higher = faster, less stable)
let STEPS = 8000;    # training steps
let CLIP  = 1.0;     # gradient clip threshold
```

| Want | Change |
|---|---|
| Faster training | `D = 32`, `CTX = 16`, `STEPS = 3000` |
| Better output | `D = 96`, `STEPS = 20000` (takes ~1 hour) |
| More stable | `LR = 0.0005`, `CLIP = 0.5` |
| More creative | `TEMP = 1.1` in the generation section |

---

## Builtin Reference

### Math

| Function | Description |
|---|---|
| `sqrt(x)` `abs(x)` | Square root, absolute value |
| `floor(x)` `ceil(x)` | Round down, round up |
| `pow(a, b)` | `a` to the power `b` |
| `min(a, b)` `max(a, b)` | Minimum, maximum |
| `exp(x)` `log(x)` | Exponential, natural log |
| `sin(x)` `cos(x)` | Trigonometry |
| `rand()` `seed(n)` | Random number, seed |

### Tensor Construction

| Function | Description |
|---|---|
| `zeros(d1, d2, ...)` | Tensor filled with 0.0 |
| `ones(d1, d2, ...)` | Tensor filled with 1.0 |
| `randn(d1, d2, ...)` | Random normal (mean 0, std 1) |
| `arange(n)` | `[0, 1, 2, ..., n-1]` |
| `shape(t)` | Shape of `t` as a 1-D tensor |
| `data(t)` | Copy of `t`'s values as a flat tensor |

### Tensor Operations

| Function | Description |
|---|---|
| `sum(t)` `mean(t)` | Sum, mean of all elements |
| `trans(t)` | Matrix transpose (2-D only) |
| `concat(a, b)` | Concatenate along columns |
| `slice(t, start, len)` | Subvector of a 1-D tensor |
| `at(t, i)` | Element at index `i` |
| `set_at(t, i, v)` | Set element `i` to `v` (in place) |
| `push(t, v)` | New tensor with `v` appended |
| `row(t, i)` | Row `i` of a 2-D tensor |
| `argmax(t)` | Index of the largest element |
| `len(t)` | Number of elements |

### Neural Network

| Function | Description |
|---|---|
| `relu(t)` `sigmoid(t)` `tanh(t)` `gelu(t)` | Activations |
| `softmax(t)` | Row-wise softmax |
| `layernorm(t)` | Row-wise layer normalization |
| `attention(Q, K, V)` | Scaled dot-product attention |
| `embedding(ids, W)` | Look up rows of `W` by index |
| `posenc(t)` | Add sinusoidal positional encodings |
| `dropout(t, p)` | Dropout with probability `p` |
| `train()` `eval_mode()` | Toggle dropout on/off |

### Loss Functions

| Function | Description |
|---|---|
| `mse(pred, target)` | Mean squared error |
| `bce(pred, target)` | Binary cross-entropy |
| `cross_entropy(logits, target)` | Softmax cross-entropy (numerically stable) |

### Autodiff and Optimizers

| Function | Description |
|---|---|
| `reset()` | Clear the autodiff tape |
| `backward(loss)` | Compute gradients |
| `grad(t)` | Gradient of `t` (zeroes the source) |
| `clip(t, limit)` | Element-wise clip to `[-limit, +limit]` |
| `adam(name, p, g, lr)` | Adam update in place |
| `sgd(p, g, lr)` | SGD update in place |
| `tapesize()` | Number of ops on the tape |
| `meminfo()` | Current tensor count and bytes |

### I/O and Tokenization

| Function | Description |
|---|---|
| `read_text(path)` | Read file as string |
| `write_text(path, s)` | Write string to file |
| `save(t, path)` | Save tensor to `.bin` |
| `load(path)` | Load tensor from `.bin` |
| `tokenize(s)` | String → tensor of byte IDs |
| `detokenize(t)` | Tensor of byte IDs → string |
| `sample(logits)` | Sample a token ID from logits |
| `chr(n)` | Integer → 1-character string |
| `str(x)` `num(x)` | Convert to string / number |
| `clock()` `sleep_ms(n)` | Wall-clock time, sleep |

---

## How It Works

Iris is a tree-walking interpreter with an embedded reverse-mode autodiff
engine.

### Pipeline

```
source.ir  →  lexer  →  tokens  →  parser  →  AST  →  evaluator  →  result
```

### Autodiff

Every tensor operation creates a new `TNode` and pushes it onto a global
**tape**. Each node records its operation, its inputs, and its output.
When you call `backward(loss)`, the evaluator walks the tape in reverse,
applying the derivative of each operation to its inputs.

```iris
reset();                    # clears the tape
let y = x @ W + b;          # tape: matmul, add
let loss = mse(y, target);  # tape: mse
backward(loss);             # walks tape in reverse
let gW = grad(W);           # W.grad is now populated
```

### Memory Management

Every tensor is registered in a global list. When you call `reset()`,
the garbage collector:

1. Clears all mark flags.
2. Marks every tensor reachable from the current environment chain.
3. Frees every unmarked tensor.

This means model weights (which live in the top-level environment) survive
across training steps, while intermediate tensors get cleaned up on every
`reset()`.

### Performance

- **Tiled matmul**: 32×32 cache blocks for better locality
- **Kahan summation**: reduces floating-point error by ~100× in accumulation
- **First-class builtins**: no dynamic dispatch overhead

On a modern phone, Iris trains a 55K-parameter transformer on 150K
characters in about 5–10 minutes.

---

## Project Structure

```
iris/
├── iris.c           # the language (~2,500 lines of C99)
├── irispoet.ir      # character-level GPT training example
├── corpus.txt       # sample training text
├── test.ir          # diagnostic script
├── README.md
└── LICENSE
```

### Build artifacts (gitignored)

```
*.bin                # saved weights
training.log         # training output
alice.txt            # downloaded corpora
```

---

## What It Is and What It Isn't

### What it **is**

- A real, working programming language.
- Correct reverse-mode autodiff.
- A working transformer architecture.
- A tool for learning how LLMs work from the inside.
- Fully self-contained: ~2,500 lines of C99, zero dependencies.

### What it **isn't**

- **Not a PyTorch replacement.** No GPU support, no distributed training.
- **Not fast.** Tree-walking interpreters are ~10,000× slower than
  hand-written CUDA kernels.
- **Not for large models.** Realistic ceiling on a phone is ~500K
  parameters, which produces only limited text.
- **Not mature.** No tooling, no ecosystem, no package manager.

Think of Iris as a **teaching language** — the same way a toy OS teaches
you how Linux works, Iris teaches you how transformers work.

---

## Contributing

Pull requests are welcome. Some areas that would benefit most:

- **Bytecode VM** — replacing the tree-walker with compiled bytecode
- **Broadcasting** — proper NumPy-style shape handling
- **BPE tokenizer** — replacing the byte-level tokenizer
- **More layers** — Conv2d, BatchNorm, LSTM, MultiHeadAttention
- **GPU backend** — emitting WGSL or CUDA for tensor ops

For larger changes, please open an issue first to discuss the design.

---

## License

MIT — see [LICENSE](LICENSE).

---

<div align="center">

**Built with curiosity, C99, and a phone.**

</div>
