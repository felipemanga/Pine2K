* console( string or number )
  Prints the given argument to the IDE's console.

* print( string or number )
  Prints the given argument to the screen at the current position, with the current color.

* printNumber( number )
  Like `print`, but only for numbers and faster.

* tile( number x, number y, bitmap or color index )
  Sets a tile to either a bitmap or a color. Bitmap must be either 8x8 or 16x16, 8bpp.

* fill( color index )
  Sets all tiles to the given color.

* tileshift( number x, number y )
  Adjusts the tilemap's pixel coordinates. Must be between 0 and 7 inclusive.

* sprite( number x, number y, bitmap )
  Draws bitmap at the specified x/y coordinates. See the color/mirror/flip commands for further customization.

* splash( string file name )
  Reads a 16-bit image file directly to the screen. Use a window to avoid erasing the result.

* mirror( boolean enable )
  Sprites will be drawn mirrored (flipped horizontally) when enabled.

* flip( boolean enable )
  Sprites will be drawn flipped vertically when enabled

* window( number start x, number start y, number end x, number end y )
  Specifies the area of the screen that should be redrawn at the end of the frame.

* random( number minimum, number maximum )
  Returns a random number. Inclusive lower bound, exclusive upper bound.

* builtin( string name )
  Returns a built-in resource (sprite/tile)

* cursor( number x, number y )
  Sets the screen coordinates for text printing.

* color( number color index )
  Sets the color to use for text/sprites/tiles.

* background( number color index )
  Sets the background color to use for text.

* pressed( string button name )
  Returns true if the specified button is pressed. Case-sensitive.
  Must be one of: A, B, C, UP, DOWN, LEFT, RIGHT.

* justPressed( string button name )
  Returns true if the specified button has just been pressed. Case-sensitive.
  Must be one of: A, B, C, UP, DOWN, LEFT, RIGHT.

* time()
  Returns the number of milliseconds since startup.

* new Array( number size )
  Creates an array of the given size and returns it.

* length( array )
  Returns the length of the given array.
  
* PEEK( pointer )
  Reads a byte from the given pointer.
  
* POKE( pointer, byte value )
  Writes the given byte to the given pointer.

* music( string raw file name )
  Plays an 8-bit 8khz raw sound file.

* highscore( number score )
  Saves the given score as the highscore if it is higher than the previous one.

* exit()
  Returns to menu.

* exec( string pine file name )
  Terminates the current script and loads another, within the same project.

* io()
  Used for reading files/resources/etc.
