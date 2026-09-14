# xScal  

xScal enchances the game's Scaleform capabilities. 
Proof of concept. 

## Source layout

- `src/api`: Callback registry and AS3 callbacks
- `src/config`: EXE profiles and updateable RVAs
- `src/hook`: Vtable stuf MovieRoot routing, and other stuff... and things
- `src/platform`: Windows memory validation and diagnostics (i'm not even using windows)
- `src/runtime`: Process initialization and shutdown, mostly.
- `src/scaleform`: Handler, ownership, and "bridge" construction

## ASM??
Yes, this extenders implements runtime forwarder. 

## Build

Just do:

```bat
build.bat
```
Needs at least MSVC 2019

Portable FCM contract tests can also be built on Linux, macOS, or Windows without producing or
loading the native proxy:

```sh
cmake -S . -B build-contract -DXSCAL_BUILD_NATIVE=OFF
cmake --build build-contract
ctest --test-dir build-contract --output-on-failure
```

The `src/fcm` target and `tests/fixtures/fcm_xscal_scenario.json` are an attributed FCM integration
extension. They model the chat and numeric input contracts without hooking a game process.

## Extending callbacks

ActionScript callbacks are defined in [`src/api/scaleform_callbacks.cpp`](src/api/scaleform_callbacks.cpp).  

# DISCLAIMER

```text
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT.

IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT, OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
