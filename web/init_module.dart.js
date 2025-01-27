(function dartProgram(){function copyProperties(a,b){var s=Object.keys(a)
for(var r=0;r<s.length;r++){var q=s[r]
b[q]=a[q]}}function mixinPropertiesHard(a,b){var s=Object.keys(a)
for(var r=0;r<s.length;r++){var q=s[r]
if(!b.hasOwnProperty(q)){b[q]=a[q]}}}function mixinPropertiesEasy(a,b){Object.assign(b,a)}var z=function(){var s=function(){}
s.prototype={p:{}}
var r=new s()
if(!(Object.getPrototypeOf(r)&&Object.getPrototypeOf(r).p===s.prototype.p))return false
try{if(typeof navigator!="undefined"&&typeof navigator.userAgent=="string"&&navigator.userAgent.indexOf("Chrome/")>=0)return true
if(typeof version=="function"&&version.length==0){var q=version()
if(/^\d+\.\d+\.\d+\.\d+$/.test(q))return true}}catch(p){}return false}()
function inherit(a,b){a.prototype.constructor=a
a.prototype["$i"+a.name]=a
if(b!=null){if(z){Object.setPrototypeOf(a.prototype,b.prototype)
return}var s=Object.create(b.prototype)
copyProperties(a.prototype,s)
a.prototype=s}}function inheritMany(a,b){for(var s=0;s<b.length;s++){inherit(b[s],a)}}function mixinEasy(a,b){mixinPropertiesEasy(b.prototype,a.prototype)
a.prototype.constructor=a}function mixinHard(a,b){mixinPropertiesHard(b.prototype,a.prototype)
a.prototype.constructor=a}function lazy(a,b,c,d){var s=a
a[b]=s
a[c]=function(){if(a[b]===s){a[b]=d()}a[c]=function(){return this[b]}
return a[b]}}function lazyFinal(a,b,c,d){var s=a
a[b]=s
a[c]=function(){if(a[b]===s){var r=d()
if(a[b]!==s){A.eZ(b)}a[b]=r}var q=a[b]
a[c]=function(){return q}
return q}}function makeConstList(a){a.$flags=7
return a}function convertToFastObject(a){function t(){}t.prototype=a
new t()
return a}function convertAllToFastObject(a){for(var s=0;s<a.length;++s){convertToFastObject(a[s])}}var y=0
function instanceTearOffGetter(a,b){var s=null
return a?function(c){if(s===null)s=A.c9(b)
return new s(c,this)}:function(){if(s===null)s=A.c9(b)
return new s(this,null)}}function staticTearOffGetter(a){var s=null
return function(){if(s===null)s=A.c9(a).prototype
return s}}var x=0
function tearOffParameters(a,b,c,d,e,f,g,h,i,j){if(typeof h=="number"){h+=x}return{co:a,iS:b,iI:c,rC:d,dV:e,cs:f,fs:g,fT:h,aI:i||0,nDA:j}}function installStaticTearOff(a,b,c,d,e,f,g,h){var s=tearOffParameters(a,true,false,c,d,e,f,g,h,false)
var r=staticTearOffGetter(s)
a[b]=r}function installInstanceTearOff(a,b,c,d,e,f,g,h,i,j){c=!!c
var s=tearOffParameters(a,false,c,d,e,f,g,h,i,!!j)
var r=instanceTearOffGetter(c,s)
a[b]=r}function setOrUpdateInterceptorsByTag(a){var s=v.interceptorsByTag
if(!s){v.interceptorsByTag=a
return}copyProperties(a,s)}function setOrUpdateLeafTags(a){var s=v.leafTags
if(!s){v.leafTags=a
return}copyProperties(a,s)}function updateTypes(a){var s=v.types
var r=s.length
s.push.apply(s,a)
return r}function updateHolder(a,b){copyProperties(b,a)
return a}var hunkHelpers=function(){var s=function(a,b,c,d,e){return function(f,g,h,i){return installInstanceTearOff(f,g,a,b,c,d,[h],i,e,false)}},r=function(a,b,c,d){return function(e,f,g,h){return installStaticTearOff(e,f,a,b,c,[g],h,d)}}
return{inherit:inherit,inheritMany:inheritMany,mixin:mixinEasy,mixinHard:mixinHard,installStaticTearOff:installStaticTearOff,installInstanceTearOff:installInstanceTearOff,_instance_0u:s(0,0,null,["$0"],0),_instance_1u:s(0,1,null,["$1"],0),_instance_2u:s(0,2,null,["$2"],0),_instance_0i:s(1,0,null,["$0"],0),_instance_1i:s(1,1,null,["$1"],0),_instance_2i:s(1,2,null,["$2"],0),_static_0:r(0,null,["$0"],0),_static_1:r(1,null,["$1"],0),_static_2:r(2,null,["$2"],0),makeConstList:makeConstList,lazy:lazy,lazyFinal:lazyFinal,updateHolder:updateHolder,convertToFastObject:convertToFastObject,updateTypes:updateTypes,setOrUpdateInterceptorsByTag:setOrUpdateInterceptorsByTag,setOrUpdateLeafTags:setOrUpdateLeafTags}}()
function initializeDeferredHunk(a){x=v.types.length
a(hunkHelpers,v,w,$)}var J={
cd(a,b,c,d){return{i:a,p:b,e:c,x:d}},
ca(a){var s,r,q,p,o,n=a[v.dispatchPropertyName]
if(n==null)if($.cb==null){A.eJ()
n=a[v.dispatchPropertyName]}if(n!=null){s=n.p
if(!1===s)return n.i
if(!0===s)return a
r=Object.getPrototypeOf(a)
if(s===r)return n.i
if(n.e===r)throw A.a(A.cu("Return interceptor for "+A.v(s(a,n))))}q=a.constructor
if(q==null)p=null
else{o=$.by
if(o==null)o=$.by=v.getIsolateTag("_$dart_js")
p=q[o]}if(p!=null)return p
p=A.eP(a)
if(p!=null)return p
if(typeof a=="function")return B.p
s=Object.getPrototypeOf(a)
if(s==null)return B.f
if(s===Object.prototype)return B.f
if(typeof q=="function"){o=$.by
if(o==null)o=$.by=v.getIsolateTag("_$dart_js")
Object.defineProperty(q,o,{value:B.c,enumerable:false,writable:true,configurable:true})
return B.c}return B.c},
an(a){if(typeof a=="number"){if(Math.floor(a)==a)return J.X.prototype
return J.aA.prototype}if(typeof a=="string")return J.Z.prototype
if(a==null)return J.Y.prototype
if(typeof a=="boolean")return J.az.prototype
if(Array.isArray(a))return J.r.prototype
if(typeof a!="object"){if(typeof a=="function")return J.C.prototype
if(typeof a=="symbol")return J.a1.prototype
if(typeof a=="bigint")return J.a_.prototype
return a}if(a instanceof A.i)return a
return J.ca(a)},
d2(a){if(typeof a=="string")return J.Z.prototype
if(a==null)return a
if(Array.isArray(a))return J.r.prototype
if(typeof a!="object"){if(typeof a=="function")return J.C.prototype
if(typeof a=="symbol")return J.a1.prototype
if(typeof a=="bigint")return J.a_.prototype
return a}if(a instanceof A.i)return a
return J.ca(a)},
eE(a){if(a==null)return a
if(Array.isArray(a))return J.r.prototype
if(typeof a!="object"){if(typeof a=="function")return J.C.prototype
if(typeof a=="symbol")return J.a1.prototype
if(typeof a=="bigint")return J.a_.prototype
return a}if(a instanceof A.i)return a
return J.ca(a)},
dk(a){return J.eE(a).gU(a)},
cf(a){return J.d2(a).gl(a)},
dl(a){return J.an(a).gi(a)},
ar(a){return J.an(a).h(a)},
ay:function ay(){},
az:function az(){},
Y:function Y(){},
a0:function a0(){},
D:function D(){},
aP:function aP(){},
a8:function a8(){},
C:function C(){},
a_:function a_(){},
a1:function a1(){},
r:function r(a){this.$ti=a},
b8:function b8(a){this.$ti=a},
as:function as(a,b,c){var _=this
_.a=a
_.b=b
_.c=0
_.d=null
_.$ti=c},
aB:function aB(){},
X:function X(){},
aA:function aA(){},
Z:function Z(){}},A={bX:function bX(){},
c8(a,b,c){return a},
eO(a){var s,r
for(s=$.aq.length,r=0;r<s;++r)if(a===$.aq[r])return!0
return!1},
aD:function aD(a){this.a=a},
aE:function aE(a,b,c){var _=this
_.a=a
_.b=b
_.c=0
_.d=null
_.$ti=c},
W:function W(){},
d8(a){var s=v.mangledGlobalNames[a]
if(s!=null)return s
return"minified:"+a},
fG(a,b){var s
if(b!=null){s=b.x
if(s!=null)return s}return t.p.b(a)},
v(a){var s
if(typeof a=="string")return a
if(typeof a=="number"){if(a!==0)return""+a}else if(!0===a)return"true"
else if(!1===a)return"false"
else if(a==null)return"null"
s=J.ar(a)
return s},
bb(a){return A.dx(a)},
dx(a){var s,r,q,p
if(a instanceof A.i)return A.n(A.ao(a),null)
s=J.an(a)
if(s===B.n||s===B.q||t.o.b(a)){r=B.d(a)
if(r!=="Object"&&r!=="")return r
q=a.constructor
if(typeof q=="function"){p=q.name
if(typeof p=="string"&&p!=="Object"&&p!=="")return p}}return A.n(A.ao(a),null)},
dz(a){if(typeof a=="number"||A.c4(a))return J.ar(a)
if(typeof a=="string")return JSON.stringify(a)
if(a instanceof A.K)return a.h(0)
return"Instance of '"+A.bb(a)+"'"},
dy(a){var s=a.$thrownJsError
if(s==null)return null
return A.H(s)},
co(a,b){var s
if(a.$thrownJsError==null){s=A.a(a)
a.$thrownJsError=s
s.stack=b.h(0)}},
cc(a,b){if(a==null)J.cf(a)
throw A.a(A.d0(a,b))},
d0(a,b){var s,r="index"
if(!A.cR(b))return new A.t(!0,b,r,null)
s=J.cf(a)
if(b<0||b>=s)return new A.ax(s,!0,b,r,"Index out of range")
return new A.aQ(!0,b,r,"Value not in range")},
a(a){return A.d4(new Error(),a)},
d4(a,b){var s
if(b==null)b=new A.w()
a.dartException=b
s=A.f_
if("defineProperty" in Object){Object.defineProperty(a,"message",{get:s})
a.name=""}else a.toString=s
return a},
f_(){return J.ar(this.dartException)},
eX(a){throw A.a(a)},
eY(a,b){throw A.d4(b,a)},
eW(a){throw A.a(A.cm(a))},
x(a){var s,r,q,p,o,n
a=A.eU(a.replace(String({}),"$receiver$"))
s=a.match(/\\\$[a-zA-Z]+\\\$/g)
if(s==null)s=A.c7([],t.s)
r=s.indexOf("\\$arguments\\$")
q=s.indexOf("\\$argumentsExpr\\$")
p=s.indexOf("\\$expr\\$")
o=s.indexOf("\\$method\\$")
n=s.indexOf("\\$receiver\\$")
return new A.bf(a.replace(new RegExp("\\\\\\$arguments\\\\\\$","g"),"((?:x|[^x])*)").replace(new RegExp("\\\\\\$argumentsExpr\\\\\\$","g"),"((?:x|[^x])*)").replace(new RegExp("\\\\\\$expr\\\\\\$","g"),"((?:x|[^x])*)").replace(new RegExp("\\\\\\$method\\\\\\$","g"),"((?:x|[^x])*)").replace(new RegExp("\\\\\\$receiver\\\\\\$","g"),"((?:x|[^x])*)"),r,q,p,o,n)},
bg(a){return function($expr$){var $argumentsExpr$="$arguments$"
try{$expr$.$method$($argumentsExpr$)}catch(s){return s.message}}(a)},
ct(a){return function($expr$){try{$expr$.$method$}catch(s){return s.message}}(a)},
bY(a,b){var s=b==null,r=s?null:b.method
return new A.aC(a,r,s?null:b.receiver)},
J(a){if(a==null)return new A.ba(a)
if(a instanceof A.V)return A.I(a,a.a)
if(typeof a!=="object")return a
if("dartException" in a)return A.I(a,a.dartException)
return A.ex(a)},
I(a,b){if(t.C.b(b))if(b.$thrownJsError==null)b.$thrownJsError=a
return b},
ex(a){var s,r,q,p,o,n,m,l,k,j,i,h,g
if(!("message" in a))return a
s=a.message
if("number" in a&&typeof a.number=="number"){r=a.number
q=r&65535
if((B.o.a1(r,16)&8191)===10)switch(q){case 438:return A.I(a,A.bY(A.v(s)+" (Error "+q+")",null))
case 445:case 5007:A.v(s)
return A.I(a,new A.a6())}}if(a instanceof TypeError){p=$.da()
o=$.db()
n=$.dc()
m=$.dd()
l=$.dg()
k=$.dh()
j=$.df()
$.de()
i=$.dj()
h=$.di()
g=p.k(s)
if(g!=null)return A.I(a,A.bY(s,g))
else{g=o.k(s)
if(g!=null){g.method="call"
return A.I(a,A.bY(s,g))}else if(n.k(s)!=null||m.k(s)!=null||l.k(s)!=null||k.k(s)!=null||j.k(s)!=null||m.k(s)!=null||i.k(s)!=null||h.k(s)!=null)return A.I(a,new A.a6())}return A.I(a,new A.aU(typeof s=="string"?s:""))}if(a instanceof RangeError){if(typeof s=="string"&&s.indexOf("call stack")!==-1)return new A.a7()
s=function(b){try{return String(b)}catch(f){}return null}(a)
return A.I(a,new A.t(!1,null,null,typeof s=="string"?s.replace(/^RangeError:\s*/,""):s))}if(typeof InternalError=="function"&&a instanceof InternalError)if(typeof s=="string"&&s==="too much recursion")return new A.a7()
return a},
H(a){var s
if(a instanceof A.V)return a.b
if(a==null)return new A.af(a)
s=a.$cachedTrace
if(s!=null)return s
s=new A.af(a)
if(typeof a==="object")a.$cachedTrace=s
return s},
ec(a,b,c,d,e,f){switch(b){case 0:return a.$0()
case 1:return a.$1(c)
case 2:return a.$2(c,d)
case 3:return a.$3(c,d,e)
case 4:return a.$4(c,d,e,f)}throw A.a(new A.bl("Unsupported number of arguments for wrapped closure"))},
am(a,b){var s=a.$identity
if(!!s)return s
s=A.eC(a,b)
a.$identity=s
return s},
eC(a,b){var s
switch(b){case 0:s=a.$0
break
case 1:s=a.$1
break
case 2:s=a.$2
break
case 3:s=a.$3
break
case 4:s=a.$4
break
default:s=null}if(s!=null)return s.bind(a)
return function(c,d,e){return function(f,g,h,i){return e(c,d,f,g,h,i)}}(a,b,A.ec)},
dt(a2){var s,r,q,p,o,n,m,l,k,j,i=a2.co,h=a2.iS,g=a2.iI,f=a2.nDA,e=a2.aI,d=a2.fs,c=a2.cs,b=d[0],a=c[0],a0=i[b],a1=a2.fT
a1.toString
s=h?Object.create(new A.bc().constructor.prototype):Object.create(new A.av(null,null).constructor.prototype)
s.$initialize=s.constructor
r=h?function static_tear_off(){this.$initialize()}:function tear_off(a3,a4){this.$initialize(a3,a4)}
s.constructor=r
r.prototype=s
s.$_name=b
s.$_target=a0
q=!h
if(q)p=A.cl(b,a0,g,f)
else{s.$static_name=b
p=a0}s.$S=A.dp(a1,h,g)
s[a]=p
for(o=p,n=1;n<d.length;++n){m=d[n]
if(typeof m=="string"){l=i[m]
k=m
m=l}else k=""
j=c[n]
if(j!=null){if(q)m=A.cl(k,m,g,f)
s[j]=m}if(n===e)o=m}s.$C=o
s.$R=a2.rC
s.$D=a2.dV
return r},
dp(a,b,c){if(typeof a=="number")return a
if(typeof a=="string"){if(b)throw A.a("Cannot compute signature for static tearoff.")
return function(d,e){return function(){return e(this,d)}}(a,A.dm)}throw A.a("Error in functionType of tearoff")},
dq(a,b,c,d){var s=A.ck
switch(b?-1:a){case 0:return function(e,f){return function(){return f(this)[e]()}}(c,s)
case 1:return function(e,f){return function(g){return f(this)[e](g)}}(c,s)
case 2:return function(e,f){return function(g,h){return f(this)[e](g,h)}}(c,s)
case 3:return function(e,f){return function(g,h,i){return f(this)[e](g,h,i)}}(c,s)
case 4:return function(e,f){return function(g,h,i,j){return f(this)[e](g,h,i,j)}}(c,s)
case 5:return function(e,f){return function(g,h,i,j,k){return f(this)[e](g,h,i,j,k)}}(c,s)
default:return function(e,f){return function(){return e.apply(f(this),arguments)}}(d,s)}},
cl(a,b,c,d){if(c)return A.ds(a,b,d)
return A.dq(b.length,d,a,b)},
dr(a,b,c,d){var s=A.ck,r=A.dn
switch(b?-1:a){case 0:throw A.a(new A.aR("Intercepted function with no arguments."))
case 1:return function(e,f,g){return function(){return f(this)[e](g(this))}}(c,r,s)
case 2:return function(e,f,g){return function(h){return f(this)[e](g(this),h)}}(c,r,s)
case 3:return function(e,f,g){return function(h,i){return f(this)[e](g(this),h,i)}}(c,r,s)
case 4:return function(e,f,g){return function(h,i,j){return f(this)[e](g(this),h,i,j)}}(c,r,s)
case 5:return function(e,f,g){return function(h,i,j,k){return f(this)[e](g(this),h,i,j,k)}}(c,r,s)
case 6:return function(e,f,g){return function(h,i,j,k,l){return f(this)[e](g(this),h,i,j,k,l)}}(c,r,s)
default:return function(e,f,g){return function(){var q=[g(this)]
Array.prototype.push.apply(q,arguments)
return e.apply(f(this),q)}}(d,r,s)}},
ds(a,b,c){var s,r
if($.ci==null)$.ci=A.ch("interceptor")
if($.cj==null)$.cj=A.ch("receiver")
s=b.length
r=A.dr(s,c,a,b)
return r},
c9(a){return A.dt(a)},
dm(a,b){return A.bE(v.typeUniverse,A.ao(a.a),b)},
ck(a){return a.a},
dn(a){return a.b},
ch(a){var s,r,q,p=new A.av("receiver","interceptor"),o=Object.getOwnPropertyNames(p)
o.$flags=1
s=o
for(o=s.length,r=0;r<o;++r){q=s[r]
if(p[q]===a)return q}throw A.a(A.bV("Field name "+a+" not found.",null))},
fH(a){throw A.a(new A.aZ(a))},
eF(a){return v.getIsolateTag(a)},
eP(a){var s,r,q,p,o,n=$.d3.$1(a),m=$.bL[n]
if(m!=null){Object.defineProperty(a,v.dispatchPropertyName,{value:m,enumerable:false,writable:true,configurable:true})
return m.i}s=$.bQ[n]
if(s!=null)return s
r=v.interceptorsByTag[n]
if(r==null){q=$.cY.$2(a,n)
if(q!=null){m=$.bL[q]
if(m!=null){Object.defineProperty(a,v.dispatchPropertyName,{value:m,enumerable:false,writable:true,configurable:true})
return m.i}s=$.bQ[q]
if(s!=null)return s
r=v.interceptorsByTag[q]
n=q}}if(r==null)return null
s=r.prototype
p=n[0]
if(p==="!"){m=A.bS(s)
$.bL[n]=m
Object.defineProperty(a,v.dispatchPropertyName,{value:m,enumerable:false,writable:true,configurable:true})
return m.i}if(p==="~"){$.bQ[n]=s
return s}if(p==="-"){o=A.bS(s)
Object.defineProperty(Object.getPrototypeOf(a),v.dispatchPropertyName,{value:o,enumerable:false,writable:true,configurable:true})
return o.i}if(p==="+")return A.d5(a,s)
if(p==="*")throw A.a(A.cu(n))
if(v.leafTags[n]===true){o=A.bS(s)
Object.defineProperty(Object.getPrototypeOf(a),v.dispatchPropertyName,{value:o,enumerable:false,writable:true,configurable:true})
return o.i}else return A.d5(a,s)},
d5(a,b){var s=Object.getPrototypeOf(a)
Object.defineProperty(s,v.dispatchPropertyName,{value:J.cd(b,s,null,null),enumerable:false,writable:true,configurable:true})
return b},
bS(a){return J.cd(a,!1,null,!!a.$io)},
eQ(a,b,c){var s=b.prototype
if(v.leafTags[a]===true)return A.bS(s)
else return J.cd(s,c,null,null)},
eJ(){if(!0===$.cb)return
$.cb=!0
A.eK()},
eK(){var s,r,q,p,o,n,m,l
$.bL=Object.create(null)
$.bQ=Object.create(null)
A.eI()
s=v.interceptorsByTag
r=Object.getOwnPropertyNames(s)
if(typeof window!="undefined"){window
q=function(){}
for(p=0;p<r.length;++p){o=r[p]
n=$.d7.$1(o)
if(n!=null){m=A.eQ(o,s[o],n)
if(m!=null){Object.defineProperty(n,v.dispatchPropertyName,{value:m,enumerable:false,writable:true,configurable:true})
q.prototype=n}}}}for(p=0;p<r.length;++p){o=r[p]
if(/^[A-Za-z_]/.test(o)){l=s[o]
s["!"+o]=l
s["~"+o]=l
s["-"+o]=l
s["+"+o]=l
s["*"+o]=l}}},
eI(){var s,r,q,p,o,n,m=B.h()
m=A.U(B.i,A.U(B.j,A.U(B.e,A.U(B.e,A.U(B.k,A.U(B.l,A.U(B.m(B.d),m)))))))
if(typeof dartNativeDispatchHooksTransformer!="undefined"){s=dartNativeDispatchHooksTransformer
if(typeof s=="function")s=[s]
if(Array.isArray(s))for(r=0;r<s.length;++r){q=s[r]
if(typeof q=="function")m=q(m)||m}}p=m.getTag
o=m.getUnknownTag
n=m.prototypeForTag
$.d3=new A.bM(p)
$.cY=new A.bN(o)
$.d7=new A.bO(n)},
U(a,b){return a(b)||b},
eD(a,b){var s=b.length,r=v.rttc[""+s+";"+a]
if(r==null)return null
if(s===0)return r
if(s===r.length)return r.apply(null,b)
return r(b)},
eU(a){if(/[[\]{}()*+?.\\^$|]/.test(a))return a.replace(/[[\]{}()*+?.\\^$|]/g,"\\$&")
return a},
bf:function bf(a,b,c,d,e,f){var _=this
_.a=a
_.b=b
_.c=c
_.d=d
_.e=e
_.f=f},
a6:function a6(){},
aC:function aC(a,b,c){this.a=a
this.b=b
this.c=c},
aU:function aU(a){this.a=a},
ba:function ba(a){this.a=a},
V:function V(a,b){this.a=a
this.b=b},
af:function af(a){this.a=a
this.b=null},
K:function K(){},
b5:function b5(){},
b6:function b6(){},
be:function be(){},
bc:function bc(){},
av:function av(a,b){this.a=a
this.b=b},
aZ:function aZ(a){this.a=a},
aR:function aR(a){this.a=a},
bM:function bM(a){this.a=a},
bN:function bN(a){this.a=a},
bO:function bO(a){this.a=a},
M(a,b,c){if(a>>>0!==a||a>=c)throw A.a(A.d0(b,a))},
aF:function aF(){},
a4:function a4(){},
aG:function aG(){},
P:function P(){},
a2:function a2(){},
a3:function a3(){},
aH:function aH(){},
aI:function aI(){},
aJ:function aJ(){},
aK:function aK(){},
aL:function aL(){},
aM:function aM(){},
aN:function aN(){},
a5:function a5(){},
aO:function aO(){},
ab:function ab(){},
ac:function ac(){},
ad:function ad(){},
ae:function ae(){},
cp(a,b){var s=b.c
return s==null?b.c=A.c1(a,b.x,!0):s},
bZ(a,b){var s=b.c
return s==null?b.c=A.ai(a,"O",[b.x]):s},
cq(a){var s=a.w
if(s===6||s===7||s===8)return A.cq(a.x)
return s===12||s===13},
dA(a){return a.as},
d1(a){return A.b3(v.typeUniverse,a,!1)},
G(a1,a2,a3,a4){var s,r,q,p,o,n,m,l,k,j,i,h,g,f,e,d,c,b,a,a0=a2.w
switch(a0){case 5:case 1:case 2:case 3:case 4:return a2
case 6:s=a2.x
r=A.G(a1,s,a3,a4)
if(r===s)return a2
return A.cF(a1,r,!0)
case 7:s=a2.x
r=A.G(a1,s,a3,a4)
if(r===s)return a2
return A.c1(a1,r,!0)
case 8:s=a2.x
r=A.G(a1,s,a3,a4)
if(r===s)return a2
return A.cD(a1,r,!0)
case 9:q=a2.y
p=A.T(a1,q,a3,a4)
if(p===q)return a2
return A.ai(a1,a2.x,p)
case 10:o=a2.x
n=A.G(a1,o,a3,a4)
m=a2.y
l=A.T(a1,m,a3,a4)
if(n===o&&l===m)return a2
return A.c_(a1,n,l)
case 11:k=a2.x
j=a2.y
i=A.T(a1,j,a3,a4)
if(i===j)return a2
return A.cE(a1,k,i)
case 12:h=a2.x
g=A.G(a1,h,a3,a4)
f=a2.y
e=A.eu(a1,f,a3,a4)
if(g===h&&e===f)return a2
return A.cC(a1,g,e)
case 13:d=a2.y
a4+=d.length
c=A.T(a1,d,a3,a4)
o=a2.x
n=A.G(a1,o,a3,a4)
if(c===d&&n===o)return a2
return A.c0(a1,n,c,!0)
case 14:b=a2.x
if(b<a4)return a2
a=a3[b-a4]
if(a==null)return a2
return a
default:throw A.a(A.au("Attempted to substitute unexpected RTI kind "+a0))}},
T(a,b,c,d){var s,r,q,p,o=b.length,n=A.bF(o)
for(s=!1,r=0;r<o;++r){q=b[r]
p=A.G(a,q,c,d)
if(p!==q)s=!0
n[r]=p}return s?n:b},
ev(a,b,c,d){var s,r,q,p,o,n,m=b.length,l=A.bF(m)
for(s=!1,r=0;r<m;r+=3){q=b[r]
p=b[r+1]
o=b[r+2]
n=A.G(a,o,c,d)
if(n!==o)s=!0
l.splice(r,3,q,p,n)}return s?l:b},
eu(a,b,c,d){var s,r=b.a,q=A.T(a,r,c,d),p=b.b,o=A.T(a,p,c,d),n=b.c,m=A.ev(a,n,c,d)
if(q===r&&o===p&&m===n)return b
s=new A.b0()
s.a=q
s.b=o
s.c=m
return s},
c7(a,b){a[v.arrayRti]=b
return a},
d_(a){var s=a.$S
if(s!=null){if(typeof s=="number")return A.eH(s)
return a.$S()}return null},
eL(a,b){var s
if(A.cq(b))if(a instanceof A.K){s=A.d_(a)
if(s!=null)return s}return A.ao(a)},
ao(a){if(a instanceof A.i)return A.cP(a)
if(Array.isArray(a))return A.c2(a)
return A.c3(J.an(a))},
c2(a){var s=a[v.arrayRti],r=t.b
if(s==null)return r
if(s.constructor!==r.constructor)return r
return s},
cP(a){var s=a.$ti
return s!=null?s:A.c3(a)},
c3(a){var s=a.constructor,r=s.$ccache
if(r!=null)return r
return A.e9(a,s)},
e9(a,b){var s=a instanceof A.K?Object.getPrototypeOf(Object.getPrototypeOf(a)).constructor:b,r=A.dZ(v.typeUniverse,s.name)
b.$ccache=r
return r},
eH(a){var s,r=v.types,q=r[a]
if(typeof q=="string"){s=A.b3(v.typeUniverse,q,!1)
r[a]=s
return s}return q},
eG(a){return A.N(A.cP(a))},
et(a){var s=a instanceof A.K?A.d_(a):null
if(s!=null)return s
if(t.R.b(a))return J.dl(a).a
if(Array.isArray(a))return A.c2(a)
return A.ao(a)},
N(a){var s=a.r
return s==null?a.r=A.cM(a):s},
cM(a){var s,r,q=a.as,p=q.replace(/\*/g,"")
if(p===q)return a.r=new A.bD(a)
s=A.b3(v.typeUniverse,p,!0)
r=s.r
return r==null?s.r=A.cM(s):r},
u(a){return A.N(A.b3(v.typeUniverse,a,!1))},
e8(a){var s,r,q,p,o,n,m=this
if(m===t.K)return A.z(m,a,A.eh)
if(!A.A(m))s=m===t._
else s=!0
if(s)return A.z(m,a,A.el)
s=m.w
if(s===7)return A.z(m,a,A.e6)
if(s===1)return A.z(m,a,A.cS)
r=s===6?m.x:m
q=r.w
if(q===8)return A.z(m,a,A.ed)
if(r===t.S)p=A.cR
else if(r===t.i||r===t.H)p=A.eg
else if(r===t.N)p=A.ej
else p=r===t.y?A.c4:null
if(p!=null)return A.z(m,a,p)
if(q===9){o=r.x
if(r.y.every(A.eM)){m.f="$i"+o
if(o==="dw")return A.z(m,a,A.ef)
return A.z(m,a,A.ek)}}else if(q===11){n=A.eD(r.x,r.y)
return A.z(m,a,n==null?A.cS:n)}return A.z(m,a,A.e4)},
z(a,b,c){a.b=c
return a.b(b)},
e7(a){var s,r=this,q=A.e3
if(!A.A(r))s=r===t._
else s=!0
if(s)q=A.e1
else if(r===t.K)q=A.e0
else{s=A.ap(r)
if(s)q=A.e5}r.a=q
return r.a(a)},
b4(a){var s=a.w,r=!0
if(!A.A(a))if(!(a===t._))if(!(a===t.A))if(s!==7)if(!(s===6&&A.b4(a.x)))r=s===8&&A.b4(a.x)||a===t.P||a===t.T
return r},
e4(a){var s=this
if(a==null)return A.b4(s)
return A.eN(v.typeUniverse,A.eL(a,s),s)},
e6(a){if(a==null)return!0
return this.x.b(a)},
ek(a){var s,r=this
if(a==null)return A.b4(r)
s=r.f
if(a instanceof A.i)return!!a[s]
return!!J.an(a)[s]},
ef(a){var s,r=this
if(a==null)return A.b4(r)
if(typeof a!="object")return!1
if(Array.isArray(a))return!0
s=r.f
if(a instanceof A.i)return!!a[s]
return!!J.an(a)[s]},
e3(a){var s=this
if(a==null){if(A.ap(s))return a}else if(s.b(a))return a
A.cN(a,s)},
e5(a){var s=this
if(a==null)return a
else if(s.b(a))return a
A.cN(a,s)},
cN(a,b){throw A.a(A.dP(A.cv(a,A.n(b,null))))},
cv(a,b){return A.b7(a)+": type '"+A.n(A.et(a),null)+"' is not a subtype of type '"+b+"'"},
dP(a){return new A.ag("TypeError: "+a)},
m(a,b){return new A.ag("TypeError: "+A.cv(a,b))},
ed(a){var s=this,r=s.w===6?s.x:s
return r.x.b(a)||A.bZ(v.typeUniverse,r).b(a)},
eh(a){return a!=null},
e0(a){if(a!=null)return a
throw A.a(A.m(a,"Object"))},
el(a){return!0},
e1(a){return a},
cS(a){return!1},
c4(a){return!0===a||!1===a},
fr(a){if(!0===a)return!0
if(!1===a)return!1
throw A.a(A.m(a,"bool"))},
ft(a){if(!0===a)return!0
if(!1===a)return!1
if(a==null)return a
throw A.a(A.m(a,"bool"))},
fs(a){if(!0===a)return!0
if(!1===a)return!1
if(a==null)return a
throw A.a(A.m(a,"bool?"))},
fu(a){if(typeof a=="number")return a
throw A.a(A.m(a,"double"))},
fw(a){if(typeof a=="number")return a
if(a==null)return a
throw A.a(A.m(a,"double"))},
fv(a){if(typeof a=="number")return a
if(a==null)return a
throw A.a(A.m(a,"double?"))},
cR(a){return typeof a=="number"&&Math.floor(a)===a},
fx(a){if(typeof a=="number"&&Math.floor(a)===a)return a
throw A.a(A.m(a,"int"))},
fz(a){if(typeof a=="number"&&Math.floor(a)===a)return a
if(a==null)return a
throw A.a(A.m(a,"int"))},
fy(a){if(typeof a=="number"&&Math.floor(a)===a)return a
if(a==null)return a
throw A.a(A.m(a,"int?"))},
eg(a){return typeof a=="number"},
fA(a){if(typeof a=="number")return a
throw A.a(A.m(a,"num"))},
fC(a){if(typeof a=="number")return a
if(a==null)return a
throw A.a(A.m(a,"num"))},
fB(a){if(typeof a=="number")return a
if(a==null)return a
throw A.a(A.m(a,"num?"))},
ej(a){return typeof a=="string"},
fD(a){if(typeof a=="string")return a
throw A.a(A.m(a,"String"))},
fF(a){if(typeof a=="string")return a
if(a==null)return a
throw A.a(A.m(a,"String"))},
fE(a){if(typeof a=="string")return a
if(a==null)return a
throw A.a(A.m(a,"String?"))},
cV(a,b){var s,r,q
for(s="",r="",q=0;q<a.length;++q,r=", ")s+=r+A.n(a[q],b)
return s},
en(a,b){var s,r,q,p,o,n,m=a.x,l=a.y
if(""===m)return"("+A.cV(l,b)+")"
s=l.length
r=m.split(",")
q=r.length-s
for(p="(",o="",n=0;n<s;++n,o=", "){p+=o
if(q===0)p+="{"
p+=A.n(l[n],b)
if(q>=0)p+=" "+r[q];++q}return p+"})"},
cO(a4,a5,a6){var s,r,q,p,o,n,m,l,k,j,i,h,g,f,e,d,c,b,a,a0,a1,a2=", ",a3=null
if(a6!=null){s=a6.length
if(a5==null)a5=A.c7([],t.s)
else a3=a5.length
r=a5.length
for(q=s;q>0;--q)a5.push("T"+(r+q))
for(p=t.X,o=t._,n="<",m="",q=0;q<s;++q,m=a2){l=a5.length
k=l-1-q
if(!(k>=0))return A.cc(a5,k)
n=n+m+a5[k]
j=a6[q]
i=j.w
if(!(i===2||i===3||i===4||i===5||j===p))l=j===o
else l=!0
if(!l)n+=" extends "+A.n(j,a5)}n+=">"}else n=""
p=a4.x
h=a4.y
g=h.a
f=g.length
e=h.b
d=e.length
c=h.c
b=c.length
a=A.n(p,a5)
for(a0="",a1="",q=0;q<f;++q,a1=a2)a0+=a1+A.n(g[q],a5)
if(d>0){a0+=a1+"["
for(a1="",q=0;q<d;++q,a1=a2)a0+=a1+A.n(e[q],a5)
a0+="]"}if(b>0){a0+=a1+"{"
for(a1="",q=0;q<b;q+=3,a1=a2){a0+=a1
if(c[q+1])a0+="required "
a0+=A.n(c[q+2],a5)+" "+c[q]}a0+="}"}if(a3!=null){a5.toString
a5.length=a3}return n+"("+a0+") => "+a},
n(a,b){var s,r,q,p,o,n,m,l=a.w
if(l===5)return"erased"
if(l===2)return"dynamic"
if(l===3)return"void"
if(l===1)return"Never"
if(l===4)return"any"
if(l===6)return A.n(a.x,b)
if(l===7){s=a.x
r=A.n(s,b)
q=s.w
return(q===12||q===13?"("+r+")":r)+"?"}if(l===8)return"FutureOr<"+A.n(a.x,b)+">"
if(l===9){p=A.ew(a.x)
o=a.y
return o.length>0?p+("<"+A.cV(o,b)+">"):p}if(l===11)return A.en(a,b)
if(l===12)return A.cO(a,b,null)
if(l===13)return A.cO(a.x,b,a.y)
if(l===14){n=a.x
m=b.length
n=m-1-n
if(!(n>=0&&n<m))return A.cc(b,n)
return b[n]}return"?"},
ew(a){var s=v.mangledGlobalNames[a]
if(s!=null)return s
return"minified:"+a},
e_(a,b){var s=a.tR[b]
for(;typeof s=="string";)s=a.tR[s]
return s},
dZ(a,b){var s,r,q,p,o,n=a.eT,m=n[b]
if(m==null)return A.b3(a,b,!1)
else if(typeof m=="number"){s=m
r=A.aj(a,5,"#")
q=A.bF(s)
for(p=0;p<s;++p)q[p]=r
o=A.ai(a,b,q)
n[b]=o
return o}else return m},
dX(a,b){return A.cG(a.tR,b)},
dW(a,b){return A.cG(a.eT,b)},
b3(a,b,c){var s,r=a.eC,q=r.get(b)
if(q!=null)return q
s=A.cA(A.cy(a,null,b,c))
r.set(b,s)
return s},
bE(a,b,c){var s,r,q=b.z
if(q==null)q=b.z=new Map()
s=q.get(c)
if(s!=null)return s
r=A.cA(A.cy(a,b,c,!0))
q.set(c,r)
return r},
dY(a,b,c){var s,r,q,p=b.Q
if(p==null)p=b.Q=new Map()
s=c.as
r=p.get(s)
if(r!=null)return r
q=A.c_(a,b,c.w===10?c.y:[c])
p.set(s,q)
return q},
y(a,b){b.a=A.e7
b.b=A.e8
return b},
aj(a,b,c){var s,r,q=a.eC.get(c)
if(q!=null)return q
s=new A.p(null,null)
s.w=b
s.as=c
r=A.y(a,s)
a.eC.set(c,r)
return r},
cF(a,b,c){var s,r=b.as+"*",q=a.eC.get(r)
if(q!=null)return q
s=A.dU(a,b,r,c)
a.eC.set(r,s)
return s},
dU(a,b,c,d){var s,r,q
if(d){s=b.w
if(!A.A(b))r=b===t.P||b===t.T||s===7||s===6
else r=!0
if(r)return b}q=new A.p(null,null)
q.w=6
q.x=b
q.as=c
return A.y(a,q)},
c1(a,b,c){var s,r=b.as+"?",q=a.eC.get(r)
if(q!=null)return q
s=A.dT(a,b,r,c)
a.eC.set(r,s)
return s},
dT(a,b,c,d){var s,r,q,p
if(d){s=b.w
r=!0
if(!A.A(b))if(!(b===t.P||b===t.T))if(s!==7)r=s===8&&A.ap(b.x)
if(r)return b
else if(s===1||b===t.A)return t.P
else if(s===6){q=b.x
if(q.w===8&&A.ap(q.x))return q
else return A.cp(a,b)}}p=new A.p(null,null)
p.w=7
p.x=b
p.as=c
return A.y(a,p)},
cD(a,b,c){var s,r=b.as+"/",q=a.eC.get(r)
if(q!=null)return q
s=A.dR(a,b,r,c)
a.eC.set(r,s)
return s},
dR(a,b,c,d){var s,r
if(d){s=b.w
if(A.A(b)||b===t.K||b===t._)return b
else if(s===1)return A.ai(a,"O",[b])
else if(b===t.P||b===t.T)return t.O}r=new A.p(null,null)
r.w=8
r.x=b
r.as=c
return A.y(a,r)},
dV(a,b){var s,r,q=""+b+"^",p=a.eC.get(q)
if(p!=null)return p
s=new A.p(null,null)
s.w=14
s.x=b
s.as=q
r=A.y(a,s)
a.eC.set(q,r)
return r},
ah(a){var s,r,q,p=a.length
for(s="",r="",q=0;q<p;++q,r=",")s+=r+a[q].as
return s},
dQ(a){var s,r,q,p,o,n=a.length
for(s="",r="",q=0;q<n;q+=3,r=","){p=a[q]
o=a[q+1]?"!":":"
s+=r+p+o+a[q+2].as}return s},
ai(a,b,c){var s,r,q,p=b
if(c.length>0)p+="<"+A.ah(c)+">"
s=a.eC.get(p)
if(s!=null)return s
r=new A.p(null,null)
r.w=9
r.x=b
r.y=c
if(c.length>0)r.c=c[0]
r.as=p
q=A.y(a,r)
a.eC.set(p,q)
return q},
c_(a,b,c){var s,r,q,p,o,n
if(b.w===10){s=b.x
r=b.y.concat(c)}else{r=c
s=b}q=s.as+(";<"+A.ah(r)+">")
p=a.eC.get(q)
if(p!=null)return p
o=new A.p(null,null)
o.w=10
o.x=s
o.y=r
o.as=q
n=A.y(a,o)
a.eC.set(q,n)
return n},
cE(a,b,c){var s,r,q="+"+(b+"("+A.ah(c)+")"),p=a.eC.get(q)
if(p!=null)return p
s=new A.p(null,null)
s.w=11
s.x=b
s.y=c
s.as=q
r=A.y(a,s)
a.eC.set(q,r)
return r},
cC(a,b,c){var s,r,q,p,o,n=b.as,m=c.a,l=m.length,k=c.b,j=k.length,i=c.c,h=i.length,g="("+A.ah(m)
if(j>0){s=l>0?",":""
g+=s+"["+A.ah(k)+"]"}if(h>0){s=l>0?",":""
g+=s+"{"+A.dQ(i)+"}"}r=n+(g+")")
q=a.eC.get(r)
if(q!=null)return q
p=new A.p(null,null)
p.w=12
p.x=b
p.y=c
p.as=r
o=A.y(a,p)
a.eC.set(r,o)
return o},
c0(a,b,c,d){var s,r=b.as+("<"+A.ah(c)+">"),q=a.eC.get(r)
if(q!=null)return q
s=A.dS(a,b,c,r,d)
a.eC.set(r,s)
return s},
dS(a,b,c,d,e){var s,r,q,p,o,n,m,l
if(e){s=c.length
r=A.bF(s)
for(q=0,p=0;p<s;++p){o=c[p]
if(o.w===1){r[p]=o;++q}}if(q>0){n=A.G(a,b,r,0)
m=A.T(a,c,r,0)
return A.c0(a,n,m,c!==m)}}l=new A.p(null,null)
l.w=13
l.x=b
l.y=c
l.as=d
return A.y(a,l)},
cy(a,b,c,d){return{u:a,e:b,r:c,s:[],p:0,n:d}},
cA(a){var s,r,q,p,o,n,m,l=a.r,k=a.s
for(s=l.length,r=0;r<s;){q=l.charCodeAt(r)
if(q>=48&&q<=57)r=A.dJ(r+1,q,l,k)
else if((((q|32)>>>0)-97&65535)<26||q===95||q===36||q===124)r=A.cz(a,r,l,k,!1)
else if(q===46)r=A.cz(a,r,l,k,!0)
else{++r
switch(q){case 44:break
case 58:k.push(!1)
break
case 33:k.push(!0)
break
case 59:k.push(A.F(a.u,a.e,k.pop()))
break
case 94:k.push(A.dV(a.u,k.pop()))
break
case 35:k.push(A.aj(a.u,5,"#"))
break
case 64:k.push(A.aj(a.u,2,"@"))
break
case 126:k.push(A.aj(a.u,3,"~"))
break
case 60:k.push(a.p)
a.p=k.length
break
case 62:A.dL(a,k)
break
case 38:A.dK(a,k)
break
case 42:p=a.u
k.push(A.cF(p,A.F(p,a.e,k.pop()),a.n))
break
case 63:p=a.u
k.push(A.c1(p,A.F(p,a.e,k.pop()),a.n))
break
case 47:p=a.u
k.push(A.cD(p,A.F(p,a.e,k.pop()),a.n))
break
case 40:k.push(-3)
k.push(a.p)
a.p=k.length
break
case 41:A.dI(a,k)
break
case 91:k.push(a.p)
a.p=k.length
break
case 93:o=k.splice(a.p)
A.cB(a.u,a.e,o)
a.p=k.pop()
k.push(o)
k.push(-1)
break
case 123:k.push(a.p)
a.p=k.length
break
case 125:o=k.splice(a.p)
A.dN(a.u,a.e,o)
a.p=k.pop()
k.push(o)
k.push(-2)
break
case 43:n=l.indexOf("(",r)
k.push(l.substring(r,n))
k.push(-4)
k.push(a.p)
a.p=k.length
r=n+1
break
default:throw"Bad character "+q}}}m=k.pop()
return A.F(a.u,a.e,m)},
dJ(a,b,c,d){var s,r,q=b-48
for(s=c.length;a<s;++a){r=c.charCodeAt(a)
if(!(r>=48&&r<=57))break
q=q*10+(r-48)}d.push(q)
return a},
cz(a,b,c,d,e){var s,r,q,p,o,n,m=b+1
for(s=c.length;m<s;++m){r=c.charCodeAt(m)
if(r===46){if(e)break
e=!0}else{if(!((((r|32)>>>0)-97&65535)<26||r===95||r===36||r===124))q=r>=48&&r<=57
else q=!0
if(!q)break}}p=c.substring(b,m)
if(e){s=a.u
o=a.e
if(o.w===10)o=o.x
n=A.e_(s,o.x)[p]
if(n==null)A.eX('No "'+p+'" in "'+A.dA(o)+'"')
d.push(A.bE(s,o,n))}else d.push(p)
return m},
dL(a,b){var s,r=a.u,q=A.cx(a,b),p=b.pop()
if(typeof p=="string")b.push(A.ai(r,p,q))
else{s=A.F(r,a.e,p)
switch(s.w){case 12:b.push(A.c0(r,s,q,a.n))
break
default:b.push(A.c_(r,s,q))
break}}},
dI(a,b){var s,r,q,p=a.u,o=b.pop(),n=null,m=null
if(typeof o=="number")switch(o){case-1:n=b.pop()
break
case-2:m=b.pop()
break
default:b.push(o)
break}else b.push(o)
s=A.cx(a,b)
o=b.pop()
switch(o){case-3:o=b.pop()
if(n==null)n=p.sEA
if(m==null)m=p.sEA
r=A.F(p,a.e,o)
q=new A.b0()
q.a=s
q.b=n
q.c=m
b.push(A.cC(p,r,q))
return
case-4:b.push(A.cE(p,b.pop(),s))
return
default:throw A.a(A.au("Unexpected state under `()`: "+A.v(o)))}},
dK(a,b){var s=b.pop()
if(0===s){b.push(A.aj(a.u,1,"0&"))
return}if(1===s){b.push(A.aj(a.u,4,"1&"))
return}throw A.a(A.au("Unexpected extended operation "+A.v(s)))},
cx(a,b){var s=b.splice(a.p)
A.cB(a.u,a.e,s)
a.p=b.pop()
return s},
F(a,b,c){if(typeof c=="string")return A.ai(a,c,a.sEA)
else if(typeof c=="number"){b.toString
return A.dM(a,b,c)}else return c},
cB(a,b,c){var s,r=c.length
for(s=0;s<r;++s)c[s]=A.F(a,b,c[s])},
dN(a,b,c){var s,r=c.length
for(s=2;s<r;s+=3)c[s]=A.F(a,b,c[s])},
dM(a,b,c){var s,r,q=b.w
if(q===10){if(c===0)return b.x
s=b.y
r=s.length
if(c<=r)return s[c-1]
c-=r
b=b.x
q=b.w}else if(c===0)return b
if(q!==9)throw A.a(A.au("Indexed base must be an interface type"))
s=b.y
if(c<=s.length)return s[c-1]
throw A.a(A.au("Bad index "+c+" for "+b.h(0)))},
eN(a,b,c){var s,r=b.d
if(r==null)r=b.d=new Map()
s=r.get(c)
if(s==null){s=A.h(a,b,null,c,null,!1)?1:0
r.set(c,s)}if(0===s)return!1
if(1===s)return!0
return!0},
h(a,b,c,d,e,f){var s,r,q,p,o,n,m,l,k,j,i
if(b===d)return!0
if(!A.A(d))s=d===t._
else s=!0
if(s)return!0
r=b.w
if(r===4)return!0
if(A.A(b))return!1
s=b.w
if(s===1)return!0
q=r===14
if(q)if(A.h(a,c[b.x],c,d,e,!1))return!0
p=d.w
s=b===t.P||b===t.T
if(s){if(p===8)return A.h(a,b,c,d.x,e,!1)
return d===t.P||d===t.T||p===7||p===6}if(d===t.K){if(r===8)return A.h(a,b.x,c,d,e,!1)
if(r===6)return A.h(a,b.x,c,d,e,!1)
return r!==7}if(r===6)return A.h(a,b.x,c,d,e,!1)
if(p===6){s=A.cp(a,d)
return A.h(a,b,c,s,e,!1)}if(r===8){if(!A.h(a,b.x,c,d,e,!1))return!1
return A.h(a,A.bZ(a,b),c,d,e,!1)}if(r===7){s=A.h(a,t.P,c,d,e,!1)
return s&&A.h(a,b.x,c,d,e,!1)}if(p===8){if(A.h(a,b,c,d.x,e,!1))return!0
return A.h(a,b,c,A.bZ(a,d),e,!1)}if(p===7){s=A.h(a,b,c,t.P,e,!1)
return s||A.h(a,b,c,d.x,e,!1)}if(q)return!1
s=r!==12
if((!s||r===13)&&d===t.Z)return!0
o=r===11
if(o&&d===t.L)return!0
if(p===13){if(b===t.g)return!0
if(r!==13)return!1
n=b.y
m=d.y
l=n.length
if(l!==m.length)return!1
c=c==null?n:n.concat(c)
e=e==null?m:m.concat(e)
for(k=0;k<l;++k){j=n[k]
i=m[k]
if(!A.h(a,j,c,i,e,!1)||!A.h(a,i,e,j,c,!1))return!1}return A.cQ(a,b.x,c,d.x,e,!1)}if(p===12){if(b===t.g)return!0
if(s)return!1
return A.cQ(a,b,c,d,e,!1)}if(r===9){if(p!==9)return!1
return A.ee(a,b,c,d,e,!1)}if(o&&p===11)return A.ei(a,b,c,d,e,!1)
return!1},
cQ(a3,a4,a5,a6,a7,a8){var s,r,q,p,o,n,m,l,k,j,i,h,g,f,e,d,c,b,a,a0,a1,a2
if(!A.h(a3,a4.x,a5,a6.x,a7,!1))return!1
s=a4.y
r=a6.y
q=s.a
p=r.a
o=q.length
n=p.length
if(o>n)return!1
m=n-o
l=s.b
k=r.b
j=l.length
i=k.length
if(o+j<n+i)return!1
for(h=0;h<o;++h){g=q[h]
if(!A.h(a3,p[h],a7,g,a5,!1))return!1}for(h=0;h<m;++h){g=l[h]
if(!A.h(a3,p[o+h],a7,g,a5,!1))return!1}for(h=0;h<i;++h){g=l[m+h]
if(!A.h(a3,k[h],a7,g,a5,!1))return!1}f=s.c
e=r.c
d=f.length
c=e.length
for(b=0,a=0;a<c;a+=3){a0=e[a]
for(;!0;){if(b>=d)return!1
a1=f[b]
b+=3
if(a0<a1)return!1
a2=f[b-2]
if(a1<a0){if(a2)return!1
continue}g=e[a+1]
if(a2&&!g)return!1
g=f[b-1]
if(!A.h(a3,e[a+2],a7,g,a5,!1))return!1
break}}for(;b<d;){if(f[b+1])return!1
b+=3}return!0},
ee(a,b,c,d,e,f){var s,r,q,p,o,n=b.x,m=d.x
for(;n!==m;){s=a.tR[n]
if(s==null)return!1
if(typeof s=="string"){n=s
continue}r=s[m]
if(r==null)return!1
q=r.length
p=q>0?new Array(q):v.typeUniverse.sEA
for(o=0;o<q;++o)p[o]=A.bE(a,b,r[o])
return A.cH(a,p,null,c,d.y,e,!1)}return A.cH(a,b.y,null,c,d.y,e,!1)},
cH(a,b,c,d,e,f,g){var s,r=b.length
for(s=0;s<r;++s)if(!A.h(a,b[s],d,e[s],f,!1))return!1
return!0},
ei(a,b,c,d,e,f){var s,r=b.y,q=d.y,p=r.length
if(p!==q.length)return!1
if(b.x!==d.x)return!1
for(s=0;s<p;++s)if(!A.h(a,r[s],c,q[s],e,!1))return!1
return!0},
ap(a){var s=a.w,r=!0
if(!(a===t.P||a===t.T))if(!A.A(a))if(s!==7)if(!(s===6&&A.ap(a.x)))r=s===8&&A.ap(a.x)
return r},
eM(a){var s
if(!A.A(a))s=a===t._
else s=!0
return s},
A(a){var s=a.w
return s===2||s===3||s===4||s===5||a===t.X},
cG(a,b){var s,r,q=Object.keys(b),p=q.length
for(s=0;s<p;++s){r=q[s]
a[r]=b[r]}},
bF(a){return a>0?new Array(a):v.typeUniverse.sEA},
p:function p(a,b){var _=this
_.a=a
_.b=b
_.r=_.f=_.d=_.c=null
_.w=0
_.as=_.Q=_.z=_.y=_.x=null},
b0:function b0(){this.c=this.b=this.a=null},
bD:function bD(a){this.a=a},
b_:function b_(){},
ag:function ag(a){this.a=a},
dD(){var s,r,q={}
if(self.scheduleImmediate!=null)return A.ey()
if(self.MutationObserver!=null&&self.document!=null){s=self.document.createElement("div")
r=self.document.createElement("span")
q.a=null
new self.MutationObserver(A.am(new A.bi(q),1)).observe(s,{childList:true})
return new A.bh(q,s,r)}else if(self.setImmediate!=null)return A.ez()
return A.eA()},
dE(a){self.scheduleImmediate(A.am(new A.bj(a),0))},
dF(a){self.setImmediate(A.am(new A.bk(a),0))},
dG(a){A.dO(0,a)},
dO(a,b){var s=new A.bB()
s.X(a,b)
return s},
cT(a){return new A.aW(new A.j($.f,a.j("j<0>")),a.j("aW<0>"))},
cL(a,b){a.$2(0,null)
b.b=!0
return b.a},
cI(a,b){A.e2(a,b)},
cK(a,b){b.H(a)},
cJ(a,b){b.I(A.J(a),A.H(a))},
e2(a,b){var s,r,q=new A.bH(b),p=new A.bI(b)
if(a instanceof A.j)a.R(q,p,t.z)
else{s=t.z
if(a instanceof A.j)a.L(q,p,s)
else{r=new A.j($.f,t.d)
r.a=8
r.c=a
r.R(q,p,s)}}},
cX(a){var s=function(b,c){return function(d,e){while(true){try{b(d,e)
break}catch(r){e=r
d=c}}}}(a,1)
return $.f.V(new A.bK(s))},
bW(a){var s
if(t.C.b(a)){s=a.gp()
if(s!=null)return s}return B.b},
ea(a,b){if($.f===B.a)return null
return null},
eb(a,b){if($.f!==B.a)A.ea(a,b)
if(b==null)if(t.C.b(a)){b=a.gp()
if(b==null){A.co(a,B.b)
b=B.b}}else b=B.b
else if(t.C.b(a))A.co(a,b)
return new A.B(a,b)},
cw(a,b){var s,r
for(;s=a.a,(s&4)!==0;)a=a.c
if(a===b){b.q(new A.t(!0,a,null,"Cannot complete a future with itself"),A.cr())
return}s|=b.a&1
a.a=s
if((s&24)!==0){r=b.G()
b.t(a)
A.aa(b,r)}else{r=b.c
b.P(a)
a.F(r)}},
dH(a,b){var s,r,q={},p=q.a=a
for(;s=p.a,(s&4)!==0;){p=p.c
q.a=p}if(p===b){b.q(new A.t(!0,p,null,"Cannot complete a future with itself"),A.cr())
return}if((s&24)===0){r=b.c
b.P(p)
q.a.F(r)
return}if((s&16)===0&&b.c==null){b.t(p)
return}b.a^=2
A.S(null,null,b.b,new A.bp(q,b))},
aa(a,b){var s,r,q,p,o,n,m,l,k,j,i,h,g={},f=g.a=a
for(;!0;){s={}
r=f.a
q=(r&16)===0
p=!q
if(b==null){if(p&&(r&1)===0){f=f.c
A.c6(f.a,f.b)}return}s.a=b
o=b.a
for(f=b;o!=null;f=o,o=n){f.a=null
A.aa(g.a,f)
s.a=o
n=o.a}r=g.a
m=r.c
s.b=p
s.c=m
if(q){l=f.c
l=(l&1)!==0||(l&15)===8}else l=!0
if(l){k=f.b.b
if(p){r=r.b===k
r=!(r||r)}else r=!1
if(r){A.c6(m.a,m.b)
return}j=$.f
if(j!==k)$.f=k
else j=null
f=f.c
if((f&15)===8)new A.bw(s,g,p).$0()
else if(q){if((f&1)!==0)new A.bv(s,m).$0()}else if((f&2)!==0)new A.bu(g,s).$0()
if(j!=null)$.f=j
f=s.c
if(f instanceof A.j){r=s.a.$ti
r=r.j("O<2>").b(f)||!r.y[1].b(f)}else r=!1
if(r){i=s.a.b
if((f.a&24)!==0){h=i.c
i.c=null
b=i.u(h)
i.a=f.a&30|i.a&1
i.c=f.c
g.a=f
continue}else A.cw(f,i)
return}}i=s.a.b
h=i.c
i.c=null
b=i.u(h)
f=s.b
r=s.c
if(!f){i.a=8
i.c=r}else{i.a=i.a&1|16
i.c=r}g.a=i
f=i}},
eo(a,b){if(t.Q.b(a))return b.V(a)
if(t.v.b(a))return a
throw A.a(A.cg(a,"onError",u.c))},
em(){var s,r
for(s=$.R;s!=null;s=$.R){$.al=null
r=s.b
$.R=r
if(r==null)$.ak=null
s.a.$0()}},
es(){$.c5=!0
try{A.em()}finally{$.al=null
$.c5=!1
if($.R!=null)$.ce().$1(A.cZ())}},
cW(a){var s=new A.aX(a),r=$.ak
if(r==null){$.R=$.ak=s
if(!$.c5)$.ce().$1(A.cZ())}else $.ak=r.b=s},
er(a){var s,r,q,p=$.R
if(p==null){A.cW(a)
$.al=$.ak
return}s=new A.aX(a)
r=$.al
if(r==null){s.b=p
$.R=$.al=s}else{q=r.b
s.b=q
$.al=r.b=s
if(q==null)$.ak=s}},
eV(a){var s=null,r=$.f
if(B.a===r){A.S(s,s,B.a,a)
return}A.S(s,s,r,r.S(a))},
fb(a){A.c8(a,"stream",t.K)
return new A.b1()},
c6(a,b){A.er(new A.bJ(a,b))},
cU(a,b,c,d){var s,r=$.f
if(r===c)return d.$0()
$.f=c
s=r
try{r=d.$0()
return r}finally{$.f=s}},
eq(a,b,c,d,e){var s,r=$.f
if(r===c)return d.$1(e)
$.f=c
s=r
try{r=d.$1(e)
return r}finally{$.f=s}},
ep(a,b,c,d,e,f){var s,r=$.f
if(r===c)return d.$2(e,f)
$.f=c
s=r
try{r=d.$2(e,f)
return r}finally{$.f=s}},
S(a,b,c,d){if(B.a!==c)d=c.S(d)
A.cW(d)},
bi:function bi(a){this.a=a},
bh:function bh(a,b,c){this.a=a
this.b=b
this.c=c},
bj:function bj(a){this.a=a},
bk:function bk(a){this.a=a},
bB:function bB(){},
bC:function bC(a,b){this.a=a
this.b=b},
aW:function aW(a,b){this.a=a
this.b=!1
this.$ti=b},
bH:function bH(a){this.a=a},
bI:function bI(a){this.a=a},
bK:function bK(a){this.a=a},
B:function B(a,b){this.a=a
this.b=b},
aY:function aY(){},
a9:function a9(a,b){this.a=a
this.$ti=b},
Q:function Q(a,b,c,d,e){var _=this
_.a=null
_.b=a
_.c=b
_.d=c
_.e=d
_.$ti=e},
j:function j(a,b){var _=this
_.a=0
_.b=a
_.c=null
_.$ti=b},
bm:function bm(a,b){this.a=a
this.b=b},
bt:function bt(a,b){this.a=a
this.b=b},
bq:function bq(a){this.a=a},
br:function br(a){this.a=a},
bs:function bs(a,b,c){this.a=a
this.b=b
this.c=c},
bp:function bp(a,b){this.a=a
this.b=b},
bo:function bo(a,b){this.a=a
this.b=b},
bn:function bn(a,b,c){this.a=a
this.b=b
this.c=c},
bw:function bw(a,b,c){this.a=a
this.b=b
this.c=c},
bx:function bx(a){this.a=a},
bv:function bv(a,b){this.a=a
this.b=b},
bu:function bu(a,b){this.a=a
this.b=b},
aX:function aX(a){this.a=a
this.b=null},
b1:function b1(){},
bG:function bG(){},
bJ:function bJ(a,b){this.a=a
this.b=b},
bz:function bz(){},
bA:function bA(a,b){this.a=a
this.b=b},
e:function e(){},
du(a,b){a=A.a(a)
a.stack=b.h(0)
throw a
throw A.a("unreachable")},
dB(a,b,c){var s=J.dk(b)
if(!s.A())return a
if(c.length===0){do a+=A.v(s.gv())
while(s.A())}else{a+=A.v(s.gv())
for(;s.A();)a=a+c+A.v(s.gv())}return a},
cr(){return A.H(new Error())},
b7(a){if(typeof a=="number"||A.c4(a)||a==null)return J.ar(a)
if(typeof a=="string")return JSON.stringify(a)
return A.dz(a)},
dv(a,b){A.c8(a,"error",t.K)
A.c8(b,"stackTrace",t.l)
A.du(a,b)},
au(a){return new A.at(a)},
bV(a,b){return new A.t(!1,null,b,a)},
cg(a,b,c){return new A.t(!0,a,b,c)},
dC(a){return new A.aV(a)},
cu(a){return new A.aT(a)},
cs(a){return new A.aS(a)},
cm(a){return new A.aw(a)},
cn(a,b,c){var s,r
if(A.eO(a))return b+"..."+c
s=new A.bd(b)
$.aq.push(a)
try{r=s
r.a=A.dB(r.a,a,", ")}finally{if(0>=$.aq.length)return A.cc($.aq,-1)
$.aq.pop()}s.a+=c
r=s.a
return r.charCodeAt(0)==0?r:r},
d6(a){A.eS(a)},
d:function d(){},
at:function at(a){this.a=a},
w:function w(){},
t:function t(a,b,c,d){var _=this
_.a=a
_.b=b
_.c=c
_.d=d},
aQ:function aQ(a,b,c,d){var _=this
_.a=a
_.b=b
_.c=c
_.d=d},
ax:function ax(a,b,c,d,e){var _=this
_.f=a
_.a=b
_.b=c
_.c=d
_.d=e},
aV:function aV(a){this.a=a},
aT:function aT(a){this.a=a},
aS:function aS(a){this.a=a},
aw:function aw(a){this.a=a},
a7:function a7(){},
bl:function bl(a){this.a=a},
l:function l(){},
i:function i(){},
b2:function b2(){},
bd:function bd(a){this.a=a},
eT(a,b){var s=new A.j($.f,b.j("j<0>")),r=new A.a9(s,b.j("a9<0>"))
a.then(A.am(new A.bT(r),1),A.am(new A.bU(r),1))
return s},
bT:function bT(a){this.a=a},
bU:function bU(a){this.a=a},
b9:function b9(a){this.a=a},
eS(a){if(typeof dartPrint=="function"){dartPrint(a)
return}if(typeof console=="object"&&typeof console.log!="undefined"){console.log(a)
return}if(typeof print=="function"){print(a)
return}throw"Unable to print message: "+String(a)},
eZ(a){A.eY(new A.aD("Field '"+a+"' has been assigned during initialization."),new Error())},
bP(){var s=0,r=A.cT(t.n),q=1,p,o,n,m,l,k,j
var $async$bP=A.cX(function(a,b){if(a===1){p=b
s=q}while(true)switch(s){case 0:q=3
l=self
o=l.Module_soloud()
s=6
return A.cI(A.eT(o,t.m),$async$bP)
case 6:n=b
l.self.Module_soloud=n
A.d6("Module_soloud initialized and set globally.")
q=1
s=5
break
case 3:q=2
j=p
m=A.J(j)
A.d6("Failed to initialize Module_soloud: "+A.v(m))
throw j
s=5
break
case 2:s=1
break
case 5:return A.cK(null,r)
case 1:return A.cJ(p,r)}})
return A.cL($async$bP,r)},
bR(){var s=0,r=A.cT(t.n)
var $async$bR=A.cX(function(a,b){if(a===1)return A.cJ(b,r)
while(true)switch(s){case 0:s=2
return A.cI(A.bP(),$async$bR)
case 2:return A.cK(null,r)}})
return A.cL($async$bR,r)}},B={}
var w=[A,J,B]
var $={}
A.bX.prototype={}
J.ay.prototype={
h(a){return"Instance of '"+A.bb(a)+"'"},
gi(a){return A.N(A.c3(this))}}
J.az.prototype={
h(a){return String(a)},
gi(a){return A.N(t.y)},
$ib:1}
J.Y.prototype={
h(a){return"null"},
$ib:1,
$il:1}
J.a0.prototype={$ik:1}
J.D.prototype={
h(a){return String(a)}}
J.aP.prototype={}
J.a8.prototype={}
J.C.prototype={
h(a){var s=a[$.d9()]
if(s==null)return this.W(a)
return"JavaScript function for "+J.ar(s)}}
J.a_.prototype={
h(a){return String(a)}}
J.a1.prototype={
h(a){return String(a)}}
J.r.prototype={
h(a){return A.cn(a,"[","]")},
gU(a){return new J.as(a,a.length,A.c2(a).j("as<1>"))},
gl(a){return a.length}}
J.b8.prototype={}
J.as.prototype={
gv(){var s=this.d
return s==null?this.$ti.c.a(s):s},
A(){var s,r=this,q=r.a,p=q.length
if(r.b!==p)throw A.a(A.eW(q))
s=r.c
if(s>=p){r.d=null
return!1}r.d=q[s]
r.c=s+1
return!0}}
J.aB.prototype={
h(a){if(a===0&&1/a<0)return"-0.0"
else return""+a},
a1(a,b){var s
if(a>0)s=this.a0(a,b)
else{s=b>31?31:b
s=a>>s>>>0}return s},
a0(a,b){return b>31?0:a>>>b},
gi(a){return A.N(t.H)},
$iq:1}
J.X.prototype={
gi(a){return A.N(t.S)},
$ib:1,
$ic:1}
J.aA.prototype={
gi(a){return A.N(t.i)},
$ib:1}
J.Z.prototype={
h(a){return a},
gi(a){return A.N(t.N)},
gl(a){return a.length},
$ib:1,
$iL:1}
A.aD.prototype={
h(a){return"LateInitializationError: "+this.a}}
A.aE.prototype={
gv(){var s=this.d
return s==null?this.$ti.c.a(s):s},
A(){var s,r=this,q=r.a,p=J.d2(q),o=p.gl(q)
if(r.b!==o)throw A.a(A.cm(q))
s=r.c
if(s>=o){r.d=null
return!1}r.d=p.m(q,s);++r.c
return!0}}
A.W.prototype={}
A.bf.prototype={
k(a){var s,r,q=this,p=new RegExp(q.a).exec(a)
if(p==null)return null
s=Object.create(null)
r=q.b
if(r!==-1)s.arguments=p[r+1]
r=q.c
if(r!==-1)s.argumentsExpr=p[r+1]
r=q.d
if(r!==-1)s.expr=p[r+1]
r=q.e
if(r!==-1)s.method=p[r+1]
r=q.f
if(r!==-1)s.receiver=p[r+1]
return s}}
A.a6.prototype={
h(a){return"Null check operator used on a null value"}}
A.aC.prototype={
h(a){var s,r=this,q="NoSuchMethodError: method not found: '",p=r.b
if(p==null)return"NoSuchMethodError: "+r.a
s=r.c
if(s==null)return q+p+"' ("+r.a+")"
return q+p+"' on '"+s+"' ("+r.a+")"}}
A.aU.prototype={
h(a){var s=this.a
return s.length===0?"Error":"Error: "+s}}
A.ba.prototype={
h(a){return"Throw of null ('"+(this.a===null?"null":"undefined")+"' from JavaScript)"}}
A.V.prototype={}
A.af.prototype={
h(a){var s,r=this.b
if(r!=null)return r
r=this.a
s=r!==null&&typeof r==="object"?r.stack:null
return this.b=s==null?"":s},
$iE:1}
A.K.prototype={
h(a){var s=this.constructor,r=s==null?null:s.name
return"Closure '"+A.d8(r==null?"unknown":r)+"'"},
gac(){return this},
$C:"$1",
$R:1,
$D:null}
A.b5.prototype={$C:"$0",$R:0}
A.b6.prototype={$C:"$2",$R:2}
A.be.prototype={}
A.bc.prototype={
h(a){var s=this.$static_name
if(s==null)return"Closure of unknown static method"
return"Closure '"+A.d8(s)+"'"}}
A.av.prototype={
h(a){return"Closure '"+this.$_name+"' of "+("Instance of '"+A.bb(this.a)+"'")}}
A.aZ.prototype={
h(a){return"Reading static variable '"+this.a+"' during its initialization"}}
A.aR.prototype={
h(a){return"RuntimeError: "+this.a}}
A.bM.prototype={
$1(a){return this.a(a)},
$S:5}
A.bN.prototype={
$2(a,b){return this.a(a,b)},
$S:6}
A.bO.prototype={
$1(a){return this.a(a)},
$S:7}
A.aF.prototype={
gi(a){return B.r},
$ib:1}
A.a4.prototype={}
A.aG.prototype={
gi(a){return B.t},
$ib:1}
A.P.prototype={
gl(a){return a.length},
$io:1}
A.a2.prototype={
m(a,b){A.M(b,a,a.length)
return a[b]}}
A.a3.prototype={}
A.aH.prototype={
gi(a){return B.u},
$ib:1}
A.aI.prototype={
gi(a){return B.v},
$ib:1}
A.aJ.prototype={
gi(a){return B.w},
m(a,b){A.M(b,a,a.length)
return a[b]},
$ib:1}
A.aK.prototype={
gi(a){return B.x},
m(a,b){A.M(b,a,a.length)
return a[b]},
$ib:1}
A.aL.prototype={
gi(a){return B.y},
m(a,b){A.M(b,a,a.length)
return a[b]},
$ib:1}
A.aM.prototype={
gi(a){return B.z},
m(a,b){A.M(b,a,a.length)
return a[b]},
$ib:1}
A.aN.prototype={
gi(a){return B.A},
m(a,b){A.M(b,a,a.length)
return a[b]},
$ib:1}
A.a5.prototype={
gi(a){return B.B},
gl(a){return a.length},
m(a,b){A.M(b,a,a.length)
return a[b]},
$ib:1}
A.aO.prototype={
gi(a){return B.C},
gl(a){return a.length},
m(a,b){A.M(b,a,a.length)
return a[b]},
$ib:1}
A.ab.prototype={}
A.ac.prototype={}
A.ad.prototype={}
A.ae.prototype={}
A.p.prototype={
j(a){return A.bE(v.typeUniverse,this,a)},
N(a){return A.dY(v.typeUniverse,this,a)}}
A.b0.prototype={}
A.bD.prototype={
h(a){return A.n(this.a,null)}}
A.b_.prototype={
h(a){return this.a}}
A.ag.prototype={$iw:1}
A.bi.prototype={
$1(a){var s=this.a,r=s.a
s.a=null
r.$0()},
$S:3}
A.bh.prototype={
$1(a){var s,r
this.a.a=a
s=this.b
r=this.c
s.firstChild?s.removeChild(r):s.appendChild(r)},
$S:8}
A.bj.prototype={
$0(){this.a.$0()},
$S:4}
A.bk.prototype={
$0(){this.a.$0()},
$S:4}
A.bB.prototype={
X(a,b){if(self.setTimeout!=null)self.setTimeout(A.am(new A.bC(this,b),0),a)
else throw A.a(A.dC("`setTimeout()` not found."))}}
A.bC.prototype={
$0(){this.b.$0()},
$S:0}
A.aW.prototype={
H(a){var s,r=this
if(a==null)a=r.$ti.c.a(a)
if(!r.b)r.a.M(a)
else{s=r.a
if(r.$ti.j("O<1>").b(a))s.O(a)
else s.C(a)}},
I(a,b){var s=this.a
if(this.b)s.n(a,b)
else s.q(a,b)}}
A.bH.prototype={
$1(a){return this.a.$2(0,a)},
$S:1}
A.bI.prototype={
$2(a,b){this.a.$2(1,new A.V(a,b))},
$S:9}
A.bK.prototype={
$2(a,b){this.a(a,b)},
$S:10}
A.B.prototype={
h(a){return A.v(this.a)},
$id:1,
gp(){return this.b}}
A.aY.prototype={
I(a,b){var s,r=this.a
if((r.a&30)!==0)throw A.a(A.cs("Future already completed"))
s=A.eb(a,b)
r.q(s.a,s.b)},
T(a){return this.I(a,null)}}
A.a9.prototype={
H(a){var s=this.a
if((s.a&30)!==0)throw A.a(A.cs("Future already completed"))
s.M(a)}}
A.Q.prototype={
a3(a){if((this.c&15)!==6)return!0
return this.b.b.K(this.d,a.a)},
a2(a){var s,r=this.e,q=null,p=a.a,o=this.b.b
if(t.Q.b(r))q=o.a7(r,p,a.b)
else q=o.K(r,p)
try{p=q
return p}catch(s){if(t.c.b(A.J(s))){if((this.c&1)!==0)throw A.a(A.bV("The error handler of Future.then must return a value of the returned future's type","onError"))
throw A.a(A.bV("The error handler of Future.catchError must return a value of the future's type","onError"))}else throw s}}}
A.j.prototype={
P(a){this.a=this.a&1|4
this.c=a},
L(a,b,c){var s,r,q=$.f
if(q===B.a){if(b!=null&&!t.Q.b(b)&&!t.v.b(b))throw A.a(A.cg(b,"onError",u.c))}else if(b!=null)b=A.eo(b,q)
s=new A.j(q,c.j("j<0>"))
r=b==null?1:3
this.B(new A.Q(s,r,a,b,this.$ti.j("@<1>").N(c).j("Q<1,2>")))
return s},
ab(a,b){return this.L(a,null,b)},
R(a,b,c){var s=new A.j($.f,c.j("j<0>"))
this.B(new A.Q(s,19,a,b,this.$ti.j("@<1>").N(c).j("Q<1,2>")))
return s},
a_(a){this.a=this.a&1|16
this.c=a},
t(a){this.a=a.a&30|this.a&1
this.c=a.c},
B(a){var s=this,r=s.a
if(r<=3){a.a=s.c
s.c=a}else{if((r&4)!==0){r=s.c
if((r.a&24)===0){r.B(a)
return}s.t(r)}A.S(null,null,s.b,new A.bm(s,a))}},
F(a){var s,r,q,p,o,n=this,m={}
m.a=a
if(a==null)return
s=n.a
if(s<=3){r=n.c
n.c=a
if(r!=null){q=a.a
for(p=a;q!=null;p=q,q=o)o=q.a
p.a=r}}else{if((s&4)!==0){s=n.c
if((s.a&24)===0){s.F(a)
return}n.t(s)}m.a=n.u(a)
A.S(null,null,n.b,new A.bt(m,n))}},
G(){var s=this.c
this.c=null
return this.u(s)},
u(a){var s,r,q
for(s=a,r=null;s!=null;r=s,s=q){q=s.a
s.a=r}return r},
Z(a){var s,r,q,p=this
p.a^=2
try{a.L(new A.bq(p),new A.br(p),t.P)}catch(q){s=A.J(q)
r=A.H(q)
A.eV(new A.bs(p,s,r))}},
C(a){var s=this,r=s.G()
s.a=8
s.c=a
A.aa(s,r)},
n(a,b){var s=this.G()
this.a_(new A.B(a,b))
A.aa(this,s)},
M(a){if(this.$ti.j("O<1>").b(a)){this.O(a)
return}this.Y(a)},
Y(a){this.a^=2
A.S(null,null,this.b,new A.bo(this,a))},
O(a){if(this.$ti.b(a)){A.dH(a,this)
return}this.Z(a)},
q(a,b){this.a^=2
A.S(null,null,this.b,new A.bn(this,a,b))},
$iO:1}
A.bm.prototype={
$0(){A.aa(this.a,this.b)},
$S:0}
A.bt.prototype={
$0(){A.aa(this.b,this.a.a)},
$S:0}
A.bq.prototype={
$1(a){var s,r,q,p=this.a
p.a^=2
try{p.C(p.$ti.c.a(a))}catch(q){s=A.J(q)
r=A.H(q)
p.n(s,r)}},
$S:3}
A.br.prototype={
$2(a,b){this.a.n(a,b)},
$S:11}
A.bs.prototype={
$0(){this.a.n(this.b,this.c)},
$S:0}
A.bp.prototype={
$0(){A.cw(this.a.a,this.b)},
$S:0}
A.bo.prototype={
$0(){this.a.C(this.b)},
$S:0}
A.bn.prototype={
$0(){this.a.n(this.b,this.c)},
$S:0}
A.bw.prototype={
$0(){var s,r,q,p,o,n,m,l=this,k=null
try{q=l.a.a
k=q.b.b.a5(q.d)}catch(p){s=A.J(p)
r=A.H(p)
if(l.c&&l.b.a.c.a===s){q=l.a
q.c=l.b.a.c}else{q=s
o=r
if(o==null)o=A.bW(q)
n=l.a
n.c=new A.B(q,o)
q=n}q.b=!0
return}if(k instanceof A.j&&(k.a&24)!==0){if((k.a&16)!==0){q=l.a
q.c=k.c
q.b=!0}return}if(k instanceof A.j){m=l.b.a
q=l.a
q.c=k.ab(new A.bx(m),t.z)
q.b=!1}},
$S:0}
A.bx.prototype={
$1(a){return this.a},
$S:12}
A.bv.prototype={
$0(){var s,r,q,p,o,n
try{q=this.a
p=q.a
q.c=p.b.b.K(p.d,this.b)}catch(o){s=A.J(o)
r=A.H(o)
q=s
p=r
if(p==null)p=A.bW(q)
n=this.a
n.c=new A.B(q,p)
n.b=!0}},
$S:0}
A.bu.prototype={
$0(){var s,r,q,p,o,n,m,l=this
try{s=l.a.a.c
p=l.b
if(p.a.a3(s)&&p.a.e!=null){p.c=p.a.a2(s)
p.b=!1}}catch(o){r=A.J(o)
q=A.H(o)
p=l.a.a.c
if(p.a===r){n=l.b
n.c=p
p=n}else{p=r
n=q
if(n==null)n=A.bW(p)
m=l.b
m.c=new A.B(p,n)
p=m}p.b=!0}},
$S:0}
A.aX.prototype={}
A.b1.prototype={}
A.bG.prototype={}
A.bJ.prototype={
$0(){A.dv(this.a,this.b)},
$S:0}
A.bz.prototype={
a9(a){var s,r,q
try{if(B.a===$.f){a.$0()
return}A.cU(null,null,this,a)}catch(q){s=A.J(q)
r=A.H(q)
A.c6(s,r)}},
S(a){return new A.bA(this,a)},
a6(a){if($.f===B.a)return a.$0()
return A.cU(null,null,this,a)},
a5(a){return this.a6(a,t.z)},
aa(a,b){if($.f===B.a)return a.$1(b)
return A.eq(null,null,this,a,b)},
K(a,b){var s=t.z
return this.aa(a,b,s,s)},
a8(a,b,c){if($.f===B.a)return a.$2(b,c)
return A.ep(null,null,this,a,b,c)},
a7(a,b,c){var s=t.z
return this.a8(a,b,c,s,s,s)},
a4(a){return a},
V(a){var s=t.z
return this.a4(a,s,s,s)}}
A.bA.prototype={
$0(){return this.a.a9(this.b)},
$S:0}
A.e.prototype={
gU(a){return new A.aE(a,this.gl(a),A.ao(a).j("aE<e.E>"))},
h(a){return A.cn(a,"[","]")}}
A.d.prototype={
gp(){return A.dy(this)}}
A.at.prototype={
h(a){var s=this.a
if(s!=null)return"Assertion failed: "+A.b7(s)
return"Assertion failed"}}
A.w.prototype={}
A.t.prototype={
gE(){return"Invalid argument"+(!this.a?"(s)":"")},
gD(){return""},
h(a){var s=this,r=s.c,q=r==null?"":" ("+r+")",p=s.d,o=p==null?"":": "+p,n=s.gE()+q+o
if(!s.a)return n
return n+s.gD()+": "+A.b7(s.gJ())},
gJ(){return this.b}}
A.aQ.prototype={
gJ(){return this.b},
gE(){return"RangeError"},
gD(){return""}}
A.ax.prototype={
gJ(){return this.b},
gE(){return"RangeError"},
gD(){if(this.b<0)return": index must not be negative"
var s=this.f
if(s===0)return": no indices are valid"
return": index should be less than "+s},
gl(a){return this.f}}
A.aV.prototype={
h(a){return"Unsupported operation: "+this.a}}
A.aT.prototype={
h(a){return"UnimplementedError: "+this.a}}
A.aS.prototype={
h(a){return"Bad state: "+this.a}}
A.aw.prototype={
h(a){return"Concurrent modification during iteration: "+A.b7(this.a)+"."}}
A.a7.prototype={
h(a){return"Stack Overflow"},
gp(){return null},
$id:1}
A.bl.prototype={
h(a){return"Exception: "+this.a}}
A.l.prototype={
h(a){return"null"}}
A.i.prototype={$ii:1,
h(a){return"Instance of '"+A.bb(this)+"'"},
gi(a){return A.eG(this)},
toString(){return this.h(this)}}
A.b2.prototype={
h(a){return""},
$iE:1}
A.bd.prototype={
gl(a){return this.a.length},
h(a){var s=this.a
return s.charCodeAt(0)==0?s:s}}
A.bT.prototype={
$1(a){return this.a.H(a)},
$S:1}
A.bU.prototype={
$1(a){if(a==null)return this.a.T(new A.b9(a===undefined))
return this.a.T(a)},
$S:1}
A.b9.prototype={
h(a){return"Promise was rejected with a value of `"+(this.a?"undefined":"null")+"`."}};(function aliases(){var s=J.D.prototype
s.W=s.h})();(function installTearOffs(){var s=hunkHelpers._static_1,r=hunkHelpers._static_0
s(A,"ey","dE",2)
s(A,"ez","dF",2)
s(A,"eA","dG",2)
r(A,"cZ","es",0)})();(function inheritance(){var s=hunkHelpers.mixin,r=hunkHelpers.inherit,q=hunkHelpers.inheritMany
r(A.i,null)
q(A.i,[A.bX,J.ay,J.as,A.d,A.aE,A.W,A.bf,A.ba,A.V,A.af,A.K,A.p,A.b0,A.bD,A.bB,A.aW,A.B,A.aY,A.Q,A.j,A.aX,A.b1,A.bG,A.e,A.a7,A.bl,A.l,A.b2,A.bd,A.b9])
q(J.ay,[J.az,J.Y,J.a0,J.a_,J.a1,J.aB,J.Z])
q(J.a0,[J.D,J.r,A.aF,A.a4])
q(J.D,[J.aP,J.a8,J.C])
r(J.b8,J.r)
q(J.aB,[J.X,J.aA])
q(A.d,[A.aD,A.w,A.aC,A.aU,A.aZ,A.aR,A.b_,A.at,A.t,A.aV,A.aT,A.aS,A.aw])
r(A.a6,A.w)
q(A.K,[A.b5,A.b6,A.be,A.bM,A.bO,A.bi,A.bh,A.bH,A.bq,A.bx,A.bT,A.bU])
q(A.be,[A.bc,A.av])
q(A.b6,[A.bN,A.bI,A.bK,A.br])
q(A.a4,[A.aG,A.P])
q(A.P,[A.ab,A.ad])
r(A.ac,A.ab)
r(A.a2,A.ac)
r(A.ae,A.ad)
r(A.a3,A.ae)
q(A.a2,[A.aH,A.aI])
q(A.a3,[A.aJ,A.aK,A.aL,A.aM,A.aN,A.a5,A.aO])
r(A.ag,A.b_)
q(A.b5,[A.bj,A.bk,A.bC,A.bm,A.bt,A.bs,A.bp,A.bo,A.bn,A.bw,A.bv,A.bu,A.bJ,A.bA])
r(A.a9,A.aY)
r(A.bz,A.bG)
q(A.t,[A.aQ,A.ax])
s(A.ab,A.e)
s(A.ac,A.W)
s(A.ad,A.e)
s(A.ae,A.W)})()
var v={typeUniverse:{eC:new Map(),tR:{},eT:{},tPV:{},sEA:[]},mangledGlobalNames:{c:"int",q:"double",eR:"num",L:"String",eB:"bool",l:"Null",dw:"List",i:"Object",f9:"Map"},mangledNames:{},types:["~()","~(@)","~(~())","l(@)","l()","@(@)","@(@,L)","@(L)","l(~())","l(@,E)","~(c,@)","l(i,E)","j<@>(@)"],interceptorsByTag:null,leafTags:null,arrayRti:Symbol("$ti")}
A.dX(v.typeUniverse,JSON.parse('{"aP":"D","a8":"D","C":"D","az":{"b":[]},"Y":{"l":[],"b":[]},"a0":{"k":[]},"D":{"k":[]},"r":{"k":[]},"b8":{"r":["1"],"k":[]},"aB":{"q":[]},"X":{"q":[],"c":[],"b":[]},"aA":{"q":[],"b":[]},"Z":{"L":[],"b":[]},"aD":{"d":[]},"a6":{"w":[],"d":[]},"aC":{"d":[]},"aU":{"d":[]},"af":{"E":[]},"aZ":{"d":[]},"aR":{"d":[]},"aF":{"k":[],"b":[]},"a4":{"k":[]},"aG":{"k":[],"b":[]},"P":{"o":["1"],"k":[]},"a2":{"e":["q"],"o":["q"],"k":[]},"a3":{"e":["c"],"o":["c"],"k":[]},"aH":{"e":["q"],"o":["q"],"k":[],"b":[],"e.E":"q"},"aI":{"e":["q"],"o":["q"],"k":[],"b":[],"e.E":"q"},"aJ":{"e":["c"],"o":["c"],"k":[],"b":[],"e.E":"c"},"aK":{"e":["c"],"o":["c"],"k":[],"b":[],"e.E":"c"},"aL":{"e":["c"],"o":["c"],"k":[],"b":[],"e.E":"c"},"aM":{"e":["c"],"o":["c"],"k":[],"b":[],"e.E":"c"},"aN":{"e":["c"],"o":["c"],"k":[],"b":[],"e.E":"c"},"a5":{"e":["c"],"o":["c"],"k":[],"b":[],"e.E":"c"},"aO":{"e":["c"],"o":["c"],"k":[],"b":[],"e.E":"c"},"b_":{"d":[]},"ag":{"w":[],"d":[]},"j":{"O":["1"]},"B":{"d":[]},"a9":{"aY":["1"]},"at":{"d":[]},"w":{"d":[]},"t":{"d":[]},"aQ":{"d":[]},"ax":{"d":[]},"aV":{"d":[]},"aT":{"d":[]},"aS":{"d":[]},"aw":{"d":[]},"a7":{"d":[]},"b2":{"E":[]}}'))
A.dW(v.typeUniverse,JSON.parse('{"W":1,"P":1,"b1":1}'))
var u={c:"Error handler must accept one Object or one Object and a StackTrace as arguments, and return a value of the returned future's type"}
var t=(function rtii(){var s=A.d1
return{C:s("d"),Z:s("f5"),s:s("r<L>"),b:s("r<@>"),T:s("Y"),m:s("k"),g:s("C"),p:s("o<@>"),P:s("l"),K:s("i"),L:s("fa"),l:s("E"),N:s("L"),R:s("b"),c:s("w"),o:s("a8"),d:s("j<@>"),y:s("eB"),i:s("q"),z:s("@"),v:s("@(i)"),Q:s("@(i,E)"),S:s("c"),A:s("0&*"),_:s("i*"),O:s("O<l>?"),X:s("i?"),H:s("eR"),n:s("~")}})();(function constants(){B.n=J.ay.prototype
B.o=J.X.prototype
B.p=J.C.prototype
B.q=J.a0.prototype
B.f=J.aP.prototype
B.c=J.a8.prototype
B.d=function getTagFallback(o) {
  var s = Object.prototype.toString.call(o);
  return s.substring(8, s.length - 1);
}
B.h=function() {
  var toStringFunction = Object.prototype.toString;
  function getTag(o) {
    var s = toStringFunction.call(o);
    return s.substring(8, s.length - 1);
  }
  function getUnknownTag(object, tag) {
    if (/^HTML[A-Z].*Element$/.test(tag)) {
      var name = toStringFunction.call(object);
      if (name == "[object Object]") return null;
      return "HTMLElement";
    }
  }
  function getUnknownTagGenericBrowser(object, tag) {
    if (object instanceof HTMLElement) return "HTMLElement";
    return getUnknownTag(object, tag);
  }
  function prototypeForTag(tag) {
    if (typeof window == "undefined") return null;
    if (typeof window[tag] == "undefined") return null;
    var constructor = window[tag];
    if (typeof constructor != "function") return null;
    return constructor.prototype;
  }
  function discriminator(tag) { return null; }
  var isBrowser = typeof HTMLElement == "function";
  return {
    getTag: getTag,
    getUnknownTag: isBrowser ? getUnknownTagGenericBrowser : getUnknownTag,
    prototypeForTag: prototypeForTag,
    discriminator: discriminator };
}
B.m=function(getTagFallback) {
  return function(hooks) {
    if (typeof navigator != "object") return hooks;
    var userAgent = navigator.userAgent;
    if (typeof userAgent != "string") return hooks;
    if (userAgent.indexOf("DumpRenderTree") >= 0) return hooks;
    if (userAgent.indexOf("Chrome") >= 0) {
      function confirm(p) {
        return typeof window == "object" && window[p] && window[p].name == p;
      }
      if (confirm("Window") && confirm("HTMLElement")) return hooks;
    }
    hooks.getTag = getTagFallback;
  };
}
B.i=function(hooks) {
  if (typeof dartExperimentalFixupGetTag != "function") return hooks;
  hooks.getTag = dartExperimentalFixupGetTag(hooks.getTag);
}
B.l=function(hooks) {
  if (typeof navigator != "object") return hooks;
  var userAgent = navigator.userAgent;
  if (typeof userAgent != "string") return hooks;
  if (userAgent.indexOf("Firefox") == -1) return hooks;
  var getTag = hooks.getTag;
  var quickMap = {
    "BeforeUnloadEvent": "Event",
    "DataTransfer": "Clipboard",
    "GeoGeolocation": "Geolocation",
    "Location": "!Location",
    "WorkerMessageEvent": "MessageEvent",
    "XMLDocument": "!Document"};
  function getTagFirefox(o) {
    var tag = getTag(o);
    return quickMap[tag] || tag;
  }
  hooks.getTag = getTagFirefox;
}
B.k=function(hooks) {
  if (typeof navigator != "object") return hooks;
  var userAgent = navigator.userAgent;
  if (typeof userAgent != "string") return hooks;
  if (userAgent.indexOf("Trident/") == -1) return hooks;
  var getTag = hooks.getTag;
  var quickMap = {
    "BeforeUnloadEvent": "Event",
    "DataTransfer": "Clipboard",
    "HTMLDDElement": "HTMLElement",
    "HTMLDTElement": "HTMLElement",
    "HTMLPhraseElement": "HTMLElement",
    "Position": "Geoposition"
  };
  function getTagIE(o) {
    var tag = getTag(o);
    var newTag = quickMap[tag];
    if (newTag) return newTag;
    if (tag == "Object") {
      if (window.DataView && (o instanceof window.DataView)) return "DataView";
    }
    return tag;
  }
  function prototypeForTagIE(tag) {
    var constructor = window[tag];
    if (constructor == null) return null;
    return constructor.prototype;
  }
  hooks.getTag = getTagIE;
  hooks.prototypeForTag = prototypeForTagIE;
}
B.j=function(hooks) {
  var getTag = hooks.getTag;
  var prototypeForTag = hooks.prototypeForTag;
  function getTagFixed(o) {
    var tag = getTag(o);
    if (tag == "Document") {
      if (!!o.xmlVersion) return "!Document";
      return "!HTMLDocument";
    }
    return tag;
  }
  function prototypeForTagFixed(tag) {
    if (tag == "Document") return null;
    return prototypeForTag(tag);
  }
  hooks.getTag = getTagFixed;
  hooks.prototypeForTag = prototypeForTagFixed;
}
B.e=function(hooks) { return hooks; }

B.a=new A.bz()
B.b=new A.b2()
B.r=A.u("f0")
B.t=A.u("f1")
B.u=A.u("f3")
B.v=A.u("f4")
B.w=A.u("f6")
B.x=A.u("f7")
B.y=A.u("f8")
B.z=A.u("fm")
B.A=A.u("fn")
B.B=A.u("fo")
B.C=A.u("fp")})();(function staticFields(){$.by=null
$.aq=A.c7([],A.d1("r<i>"))
$.cj=null
$.ci=null
$.d3=null
$.cY=null
$.d7=null
$.bL=null
$.bQ=null
$.cb=null
$.R=null
$.ak=null
$.al=null
$.c5=!1
$.f=B.a})();(function lazyInitializers(){var s=hunkHelpers.lazyFinal
s($,"f2","d9",()=>A.eF("_$dart_dartClosure"))
s($,"fc","da",()=>A.x(A.bg({
toString:function(){return"$receiver$"}})))
s($,"fd","db",()=>A.x(A.bg({$method$:null,
toString:function(){return"$receiver$"}})))
s($,"fe","dc",()=>A.x(A.bg(null)))
s($,"ff","dd",()=>A.x(function(){var $argumentsExpr$="$arguments$"
try{null.$method$($argumentsExpr$)}catch(r){return r.message}}()))
s($,"fi","dg",()=>A.x(A.bg(void 0)))
s($,"fj","dh",()=>A.x(function(){var $argumentsExpr$="$arguments$"
try{(void 0).$method$($argumentsExpr$)}catch(r){return r.message}}()))
s($,"fh","df",()=>A.x(A.ct(null)))
s($,"fg","de",()=>A.x(function(){try{null.$method$}catch(r){return r.message}}()))
s($,"fl","dj",()=>A.x(A.ct(void 0)))
s($,"fk","di",()=>A.x(function(){try{(void 0).$method$}catch(r){return r.message}}()))
s($,"fq","ce",()=>A.dD())})();(function nativeSupport(){!function(){var s=function(a){var m={}
m[a]=1
return Object.keys(hunkHelpers.convertToFastObject(m))[0]}
v.getIsolateTag=function(a){return s("___dart_"+a+v.isolateTag)}
var r="___dart_isolate_tags_"
var q=Object[r]||(Object[r]=Object.create(null))
var p="_ZxYxX"
for(var o=0;;o++){var n=s(p+"_"+o+"_")
if(!(n in q)){q[n]=1
v.isolateTag=n
break}}v.dispatchPropertyName=v.getIsolateTag("dispatch_record")}()
hunkHelpers.setOrUpdateInterceptorsByTag({ArrayBuffer:A.aF,ArrayBufferView:A.a4,DataView:A.aG,Float32Array:A.aH,Float64Array:A.aI,Int16Array:A.aJ,Int32Array:A.aK,Int8Array:A.aL,Uint16Array:A.aM,Uint32Array:A.aN,Uint8ClampedArray:A.a5,CanvasPixelArray:A.a5,Uint8Array:A.aO})
hunkHelpers.setOrUpdateLeafTags({ArrayBuffer:true,ArrayBufferView:false,DataView:true,Float32Array:true,Float64Array:true,Int16Array:true,Int32Array:true,Int8Array:true,Uint16Array:true,Uint32Array:true,Uint8ClampedArray:true,CanvasPixelArray:true,Uint8Array:false})
A.P.$nativeSuperclassTag="ArrayBufferView"
A.ab.$nativeSuperclassTag="ArrayBufferView"
A.ac.$nativeSuperclassTag="ArrayBufferView"
A.a2.$nativeSuperclassTag="ArrayBufferView"
A.ad.$nativeSuperclassTag="ArrayBufferView"
A.ae.$nativeSuperclassTag="ArrayBufferView"
A.a3.$nativeSuperclassTag="ArrayBufferView"})()
Function.prototype.$2=function(a,b){return this(a,b)}
Function.prototype.$0=function(){return this()}
Function.prototype.$1=function(a){return this(a)}
Function.prototype.$3=function(a,b,c){return this(a,b,c)}
Function.prototype.$4=function(a,b,c,d){return this(a,b,c,d)}
Function.prototype.$1$1=function(a){return this(a)}
convertAllToFastObject(w)
convertToFastObject($);(function(a){if(typeof document==="undefined"){a(null)
return}if(typeof document.currentScript!="undefined"){a(document.currentScript)
return}var s=document.scripts
function onLoad(b){for(var q=0;q<s.length;++q){s[q].removeEventListener("load",onLoad,false)}a(b.target)}for(var r=0;r<s.length;++r){s[r].addEventListener("load",onLoad,false)}})(function(a){v.currentScript=a
var s=A.bR
if(typeof dartMainRunner==="function"){dartMainRunner(s,[])}else{s([])}})})()
//# sourceMappingURL=init_module.dart.js.map
