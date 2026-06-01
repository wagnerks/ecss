

# File javadoc-banner.h

[**File List**](files.md) **>** [**doxygen-1.14.0**](dir_9d5bad020669189c90cda983471be5d0.md) **>** [**examples**](dir_8400fc686cf1eec637c6139505ac43d7.md) **>** [**javadoc-banner.h**](javadoc-banner_8h.md)

[Go to the documentation of this file](javadoc-banner_8h.md)


```C++

void cstyle( int theory );

/******************************************************************************
 * A brief history of JavaDoc-style (C-style) banner comments.
 *
 * This is the typical JavaDoc-style C-style "banner" comment. It starts with
 * a forward slash followed by some number, n, of asterisks, where n > 2. It's
 * written this way to be more "visible" to developers who are reading the
 * source code.
 *
 * Often, developers are unaware that this is not (by default) a valid Doxygen
 * comment block!
 *
 * However, as long as JAVADOC_BANNER = YES is added to the Doxyfile, it will
 * work as expected.
 *
 * This style of commenting behaves well with clang-format.
 *
 * @param theory Even if there is only one possible unified theory. it is just a
 *               set of rules and equations.
 ******************************************************************************/
void javadocBanner( int theory );

/**************************************************************************/
void doxygenBanner( int theory );
```


