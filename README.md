<span style="color:red">**This is an initial release. While we believe it to be robust and correct, at the time of writing (2021-10-15), it has not been rigorously tested and must be used with care**</span>

GS1 Digital Link URI parser
===========================

The GS1 Digital Link URI parser is a simple library for extracting AI element
data from an uncompressed Digital Link URI and presenting it in a number of
formats:

  * Unbracketed element string
  * Bracketed element string
  * JSON

Optionally each representation can be sorted such that the predefined fixed-length AIs appear first.

Optionally the unbracketed representation can have FNC1 separators (represented
by the "^" character) in just the required locations, or as separators for all
AIs.


License
-------

Copyright (c) 2000-2023 GS1 AISBL

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this library except in compliance with the License.

You may obtain a copy of the License at:

<http://www.apache.org/licenses/LICENSE-2.0>

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.


Documentation
-------------

The interface is straightforward. Refer to the `gs1dlparser.h` header file fpr API documentation and
the `example.c` commandline tool for example use.


### Linux and MacOS

To build the library and run the unit tests:

    make test

To specify a compiler:

    make test CC=clang

To build with runtime analyzers (requires LLVM):

    make test SANITIZE=yes

To fuzz the input to the parser run the following and follow the emitted instructions (requires LLVM libfuzzer):

    make fuzzer

To build and run the example console command:

    make
    ./example-bin 'https://id.gs1.org/01/09520123456788/10/ABC%2F123/21/12345?17=180426'
 
Add `DEBUG=yes` to any of the above to cause the library to emit a detailed trace
of the parse.


### Windows

The Visual Studio solution contains two projects:

  * Unit tests
  * Example console command

Within either of these two projects the `Debug` configuration causes the
library to emit a detailed trace of the parse.


Limitations
-----------

The code implements a lightweight parser that is intended for applications that are subject to infrequent change and must extract AI data from uncompressed Digital Link URIs. The intended purpose is to perform an initial extraction of AI data from a Digital Link URI and present the AI data in common formats for subsequent validation and onwards processing by other code.

It does not embed an AI table since doing so would bloat the code size and require frequent maintenance whenever a new AI is defined.

It does include the list of AIs designated as Digital Link primary keys since this is required to identify the start of AI data in a URI. New keys may be introduced periodically, but not with the same frequency as general AI additions.

As such it has the following limitations:

  * It does not support "compressed" GS1 Digital Link URIs.
  * It does not support the (deprecated) "developer-friendly" AI names feature, e.g. "/gtin/" instead of "/01/".
  * It does not validate the key-qualifier associations (and orderings) with the primary key, nor perform any other form of AI relationship validation that would require a table of AI rules to be incorporated.
  * It does not perform any validation of AI element data; neither whether the AI is assigned, nor whether an AI value follows the rules for the AI.
    * It DOES reject URIs that contain numeric-only components (non-stem path parts or query parameters) that are not 2-4 characters in length that would otherwise be misidentified as an invalid-length AI.
    * It DOES ignore any non-numeric query parameters, as required by the specification.
