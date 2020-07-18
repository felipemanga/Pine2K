# The PINE Language

PINE stands for "PINE Is Not ECMAScript." Telling you what it *isn't* actually says something about what it is:
- While the syntax may look a lot like JavaScript (aka ECMAScript), it's strictly a structured language. No OOP at all.
- PINE is *compiled* to native binary code on the Pokitto, so it's not a scripting language by certain definitions.
- It's in no-way related to Pine Script

With that out of the way, let's have a closer look at what PINE is:
PINE is a curly-brace language with optional semicolons, like JavaScript.


## Literals

PINE supports the following literals:
- Strings: `example1`, "example2", 'example3'
- Decimals: 1, -2, 9000
- Hexadecimal: 0xDUDE
- Binary: 0b11110000
- Arrays: [1, 0b10, "3", [0x4, -5]]


## Functions

Use the `function` keyword to declare a function with up to 7 arguments:

```js
function bacon( arg1, arg2, arg3 ){
}
```

Support for more/optional arguments may come in a future update. 
Arguments are passed by value. The result of a function can be specified with the `return` keyword:

```js
function getRandomNumber(){
    return 4; // chosen by fair dice roll. Guaranteed to be random.
}
```


## Variables

Variables can either belong in the global scope, where they can be accessed by all functions, or local to the functions they're declared in. To declare a variable you use the `var` keyword:
`var x, y = 1;`

Here both `x` and `y` are declared at once. The variable `x` is automatically initialized with zero, while `y` is set to 1. 

If you don't declare your variables, they are created automatically in the global scope:

```js

function beep(){
    return y + 1; // undeclared global variable is created automatically
}

```

Note that variables can be either in the global scope or in a function's scope, just as in JavaScript. That means that the following is valid:

```js

function boop(x){
  if(x){
    var y = x + 1;
  }
  return y;
}

```

Since the `y` variable belongs to the function's scope it can be accessed outside the `if`.

Note that variables can reference functions:

```js
function A(){ console("A"); }
var a = A;
a();
```


## Constants

Code that makes use of constants whenever possible will often be smaller and faster than if it used variables. To declare a constant, use the `const` keyword:

```js

const width = 10;
const height = 20;
const area = width * height;

```


Note that, unlike JavaScript, constants currently follow the same variable scoping rules. Another difference is that constants must be initialized with a value that is known at compile-time, similar to C++'s `consteval`.


## Arrays

You can create an array using the constructor:

```js
const x = new Array( 10 ); // creates an array of 10 elements
```

or using the literal syntax with up to 512 elements:

```js
const x = [1, 2, 3, 4, 5, 6, 7, 8];
```

or by reading it from a file:

```js
const x = file("data");
```

Arrays are fixed size and there is no bounds checking.


## Operators

For multiple reasons, PINE does not have operator precedence. Instead, operations are performed left-to-right, so be sure to use parenthesis wherever necessary:

```js
console(1 + 2 * 3) // prints 9, not 7!
console(1 + (2 * 3)) // prints 7
```

Note that PINE does not support String operations. All strings are immutable. Also not supported is the "?" operator. Other than that, the operators are the same as those found in C or JavaScript. This includes support for the `>>>` operator that does an unsigned shift right:

```js
console(-1 >>> 1) // prints 2147483647
```


## Flow Control

Just like other curly-brace languages, `if` works as expected:

```js
if( expression ){
  // expression is true
}else if( other ){
  // expression is false and other is true
}else{
  // none of the above
}
```

The curly-braces are optional:
```js
if( expression ) doSomething()
```

`switch` is not currently supported.


## Loops

These are the kinds of looping constructs currently supported:

```js
do {
  // repeat until expression is false
} while( expression )
```

```js
while( expression ){
  // repeats if expression is true
}
```

```js
for( declarations ; condition expression ; expression ){
  // repeats while expression is true
}
```

```js
for( var variable in array ){
  // iterates over the keys of the array (variable is 0 to length - 1)
}
```

```js
for( var variable of array ){
  // iterates over the values of the array (variable is array[0] to array[length - 1])
}
```

When inside a loop you can `break` out or `continue` to the next iteration.


