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
if(a[b]!==s){A.h3(b)}a[b]=r}var q=a[b]
a[c]=function(){return q}
return q}}function makeConstList(a){a.$flags=7
return a}function convertToFastObject(a){function t(){}t.prototype=a
new t()
return a}function convertAllToFastObject(a){for(var s=0;s<a.length;++s){convertToFastObject(a[s])}}var y=0
function instanceTearOffGetter(a,b){var s=null
return a?function(c){if(s===null)s=A.cY(b)
return new s(c,this)}:function(){if(s===null)s=A.cY(b)
return new s(this,null)}}function staticTearOffGetter(a){var s=null
return function(){if(s===null)s=A.cY(a).prototype
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
d4(a,b,c,d){return{i:a,p:b,e:c,x:d}},
d0(a){var s,r,q,p,o,n=a[v.dispatchPropertyName]
if(n==null)if($.d1==null){A.fS()
n=a[v.dispatchPropertyName]}if(n!=null){s=n.p
if(!1===s)return n.i
if(!0===s)return a
r=Object.getPrototypeOf(a)
if(s===r)return n.i
if(n.e===r)throw A.b(A.dr("Return interceptor for "+A.o(s(a,n))))}q=a.constructor
if(q==null)p=null
else{o=$.ck
if(o==null)o=$.ck=v.getIsolateTag("_$dart_js")
p=q[o]}if(p!=null)return p
p=A.fY(a)
if(p!=null)return p
if(typeof a=="function")return B.q
s=Object.getPrototypeOf(a)
if(s==null)return B.e
if(s===Object.prototype)return B.e
if(typeof q=="function"){o=$.ck
if(o==null)o=$.ck=v.getIsolateTag("_$dart_js")
Object.defineProperty(q,o,{value:B.b,enumerable:false,writable:true,configurable:true})
return B.b}return B.b},
af(a){if(typeof a=="number"){if(Math.floor(a)==a)return J.aj.prototype
return J.b4.prototype}if(typeof a=="string")return J.al.prototype
if(a==null)return J.ak.prototype
if(typeof a=="boolean")return J.b3.prototype
if(Array.isArray(a))return J.u.prototype
if(typeof a!="object"){if(typeof a=="function")return J.L.prototype
if(typeof a=="symbol")return J.ao.prototype
if(typeof a=="bigint")return J.am.prototype
return a}if(a instanceof A.d)return a
return J.d0(a)},
dX(a){if(typeof a=="string")return J.al.prototype
if(a==null)return a
if(Array.isArray(a))return J.u.prototype
if(typeof a!="object"){if(typeof a=="function")return J.L.prototype
if(typeof a=="symbol")return J.ao.prototype
if(typeof a=="bigint")return J.am.prototype
return a}if(a instanceof A.d)return a
return J.d0(a)},
d_(a){if(a==null)return a
if(Array.isArray(a))return J.u.prototype
if(typeof a!="object"){if(typeof a=="function")return J.L.prototype
if(typeof a=="symbol")return J.ao.prototype
if(typeof a=="bigint")return J.am.prototype
return a}if(a instanceof A.d)return a
return J.d0(a)},
ee(a,b){return J.d_(a).E(a,b)},
da(a){return J.af(a).gn(a)},
ef(a){return J.d_(a).gq(a)},
cL(a){return J.dX(a).gk(a)},
db(a){return J.af(a).gj(a)},
eg(a,b,c){return J.d_(a).F(a,b,c)},
aV(a){return J.af(a).h(a)},
b2:function b2(){},
b3:function b3(){},
ak:function ak(){},
an:function an(){},
M:function M(){},
bi:function bi(){},
ay:function ay(){},
L:function L(){},
am:function am(){},
ao:function ao(){},
u:function u(a){this.$ti=a},
bO:function bO(a){this.$ti=a},
aX:function aX(a,b,c){var _=this
_.a=a
_.b=b
_.c=0
_.d=null
_.$ti=c},
b5:function b5(){},
aj:function aj(){},
b4:function b4(){},
al:function al(){}},A={cP:function cP(){},
cX(a,b,c){return a},
d2(a){var s,r
for(s=$.w.length,r=0;r<s;++r)if(a===$.w[r])return!0
return!1},
ev(a,b,c,d){if(t.V.b(a))return new A.ag(a,b,c.i("@<0>").u(d).i("ag<1,2>"))
return new A.U(a,b,c.i("@<0>").u(d).i("U<1,2>"))},
ap:function ap(a){this.a=a},
e:function e(){},
N:function N(){},
a0:function a0(a,b,c){var _=this
_.a=a
_.b=b
_.c=0
_.d=null
_.$ti=c},
U:function U(a,b,c){this.a=a
this.b=b
this.$ti=c},
ag:function ag(a,b,c){this.a=a
this.b=b
this.$ti=c},
b7:function b7(a,b,c){var _=this
_.a=null
_.b=a
_.c=b
_.$ti=c},
D:function D(a,b,c){this.a=a
this.b=b
this.$ti=c},
ai:function ai(){},
e3(a){var s=v.mangledGlobalNames[a]
if(s!=null)return s
return"minified:"+a},
hC(a,b){var s
if(b!=null){s=b.x
if(s!=null)return s}return t.p.b(a)},
o(a){var s
if(typeof a=="string")return a
if(typeof a=="number"){if(a!==0)return""+a}else if(!0===a)return"true"
else if(!1===a)return"false"
else if(a==null)return"null"
s=J.aV(a)
return s},
av(a){var s,r=$.dj
if(r==null)r=$.dj=Symbol("identityHashCode")
s=a[r]
if(s==null){s=Math.random()*0x3fffffff|0
a[r]=s}return s},
bR(a){return A.ew(a)},
ew(a){var s,r,q,p
if(a instanceof A.d)return A.t(A.Z(a),null)
s=J.af(a)
if(s===B.n||s===B.r||t.o.b(a)){r=B.c(a)
if(r!=="Object"&&r!=="")return r
q=a.constructor
if(typeof q=="function"){p=q.name
if(typeof p=="string"&&p!=="Object"&&p!=="")return p}}return A.t(A.Z(a),null)},
ey(a){if(typeof a=="number"||A.cy(a))return J.aV(a)
if(typeof a=="string")return JSON.stringify(a)
if(a instanceof A.T)return a.h(0)
return"Instance of '"+A.bR(a)+"'"},
ex(a){var s=a.$thrownJsError
if(s==null)return null
return A.J(s)},
C(a,b){if(a==null)J.cL(a)
throw A.b(A.dV(a,b))},
dV(a,b){var s,r="index"
if(!A.dM(b))return new A.A(!0,b,r,null)
s=J.cL(a)
if(b<0||b>=s)return A.eq(b,s,a,r)
return new A.aw(null,null,!0,b,r,"Value not in range")},
b(a){return A.dZ(new Error(),a)},
dZ(a,b){var s
if(b==null)b=new A.F()
a.dartException=b
s=A.h5
if("defineProperty" in Object){Object.defineProperty(a,"message",{get:s})
a.name=""}else a.toString=s
return a},
h5(){return J.aV(this.dartException)},
bE(a){throw A.b(a)},
d7(a,b){throw A.dZ(b,a)},
h4(a,b,c){var s
if(b==null)b=0
if(c==null)c=0
s=Error()
A.d7(A.f9(a,b,c),s)},
f9(a,b,c){var s,r,q,p,o,n,m,l,k
if(typeof b=="string")s=b
else{r="[]=;add;removeWhere;retainWhere;removeRange;setRange;setInt8;setInt16;setInt32;setUint8;setUint16;setUint32;setFloat32;setFloat64".split(";")
q=r.length
p=b
if(p>q){c=p/q|0
p%=q}s=r[p]}o=typeof c=="string"?c:"modify;remove from;add to".split(";")[c]
n=t.j.b(a)?"list":"ByteData"
m=a.$flags|0
l="a "
if((m&4)!==0)k="constant "
else if((m&2)!==0){k="unmodifiable "
l="an "}else k=(m&1)!==0?"fixed-length ":""
return new A.az("'"+s+"': Cannot "+o+" "+l+k+n)},
h2(a){throw A.b(A.bH(a))},
G(a){var s,r,q,p,o,n
a=A.h1(a.replace(String({}),"$receiver$"))
s=a.match(/\\\$[a-zA-Z]+\\\$/g)
if(s==null)s=A.bD([],t.s)
r=s.indexOf("\\$arguments\\$")
q=s.indexOf("\\$argumentsExpr\\$")
p=s.indexOf("\\$expr\\$")
o=s.indexOf("\\$method\\$")
n=s.indexOf("\\$receiver\\$")
return new A.bW(a.replace(new RegExp("\\\\\\$arguments\\\\\\$","g"),"((?:x|[^x])*)").replace(new RegExp("\\\\\\$argumentsExpr\\\\\\$","g"),"((?:x|[^x])*)").replace(new RegExp("\\\\\\$expr\\\\\\$","g"),"((?:x|[^x])*)").replace(new RegExp("\\\\\\$method\\\\\\$","g"),"((?:x|[^x])*)").replace(new RegExp("\\\\\\$receiver\\\\\\$","g"),"((?:x|[^x])*)"),r,q,p,o,n)},
bX(a){return function($expr$){var $argumentsExpr$="$arguments$"
try{$expr$.$method$($argumentsExpr$)}catch(s){return s.message}}(a)},
dq(a){return function($expr$){try{$expr$.$method$}catch(s){return s.message}}(a)},
cQ(a,b){var s=b==null,r=s?null:b.method
return new A.b6(a,r,s?null:b.receiver)},
R(a){if(a==null)return new A.bQ(a)
if(a instanceof A.ah)return A.Q(a,a.a)
if(typeof a!=="object")return a
if("dartException" in a)return A.Q(a,a.dartException)
return A.fE(a)},
Q(a,b){if(t.Q.b(b))if(b.$thrownJsError==null)b.$thrownJsError=a
return b},
fE(a){var s,r,q,p,o,n,m,l,k,j,i,h,g
if(!("message" in a))return a
s=a.message
if("number" in a&&typeof a.number=="number"){r=a.number
q=r&65535
if((B.p.aE(r,16)&8191)===10)switch(q){case 438:return A.Q(a,A.cQ(A.o(s)+" (Error "+q+")",null))
case 445:case 5007:A.o(s)
return A.Q(a,new A.au())}}if(a instanceof TypeError){p=$.e4()
o=$.e5()
n=$.e6()
m=$.e7()
l=$.ea()
k=$.eb()
j=$.e9()
$.e8()
i=$.ed()
h=$.ec()
g=p.t(s)
if(g!=null)return A.Q(a,A.cQ(s,g))
else{g=o.t(s)
if(g!=null){g.method="call"
return A.Q(a,A.cQ(s,g))}else if(n.t(s)!=null||m.t(s)!=null||l.t(s)!=null||k.t(s)!=null||j.t(s)!=null||m.t(s)!=null||i.t(s)!=null||h.t(s)!=null)return A.Q(a,new A.au())}return A.Q(a,new A.bm(typeof s=="string"?s:""))}if(a instanceof RangeError){if(typeof s=="string"&&s.indexOf("call stack")!==-1)return new A.ax()
s=function(b){try{return String(b)}catch(f){}return null}(a)
return A.Q(a,new A.A(!1,null,null,typeof s=="string"?s.replace(/^RangeError:\s*/,""):s))}if(typeof InternalError=="function"&&a instanceof InternalError)if(typeof s=="string"&&s==="too much recursion")return new A.ax()
return a},
J(a){var s
if(a instanceof A.ah)return a.b
if(a==null)return new A.aL(a)
s=a.$cachedTrace
if(s!=null)return s
s=new A.aL(a)
if(typeof a==="object")a.$cachedTrace=s
return s},
d5(a){if(a==null)return J.da(a)
if(typeof a=="object")return A.av(a)
return J.da(a)},
fh(a,b,c,d,e,f){switch(b){case 0:return a.$0()
case 1:return a.$1(c)
case 2:return a.$2(c,d)
case 3:return a.$3(c,d,e)
case 4:return a.$4(c,d,e,f)}throw A.b(new A.c7("Unsupported number of arguments for wrapped closure"))},
cC(a,b){var s=a.$identity
if(!!s)return s
s=A.fM(a,b)
a.$identity=s
return s},
fM(a,b){var s
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
return function(c,d,e){return function(f,g,h,i){return e(c,d,f,g,h,i)}}(a,b,A.fh)},
en(a2){var s,r,q,p,o,n,m,l,k,j,i=a2.co,h=a2.iS,g=a2.iI,f=a2.nDA,e=a2.aI,d=a2.fs,c=a2.cs,b=d[0],a=c[0],a0=i[b],a1=a2.fT
a1.toString
s=h?Object.create(new A.bS().constructor.prototype):Object.create(new A.b_(null,null).constructor.prototype)
s.$initialize=s.constructor
r=h?function static_tear_off(){this.$initialize()}:function tear_off(a3,a4){this.$initialize(a3,a4)}
s.constructor=r
r.prototype=s
s.$_name=b
s.$_target=a0
q=!h
if(q)p=A.dh(b,a0,g,f)
else{s.$static_name=b
p=a0}s.$S=A.ej(a1,h,g)
s[a]=p
for(o=p,n=1;n<d.length;++n){m=d[n]
if(typeof m=="string"){l=i[m]
k=m
m=l}else k=""
j=c[n]
if(j!=null){if(q)m=A.dh(k,m,g,f)
s[j]=m}if(n===e)o=m}s.$C=o
s.$R=a2.rC
s.$D=a2.dV
return r},
ej(a,b,c){if(typeof a=="number")return a
if(typeof a=="string"){if(b)throw A.b("Cannot compute signature for static tearoff.")
return function(d,e){return function(){return e(this,d)}}(a,A.eh)}throw A.b("Error in functionType of tearoff")},
ek(a,b,c,d){var s=A.dg
switch(b?-1:a){case 0:return function(e,f){return function(){return f(this)[e]()}}(c,s)
case 1:return function(e,f){return function(g){return f(this)[e](g)}}(c,s)
case 2:return function(e,f){return function(g,h){return f(this)[e](g,h)}}(c,s)
case 3:return function(e,f){return function(g,h,i){return f(this)[e](g,h,i)}}(c,s)
case 4:return function(e,f){return function(g,h,i,j){return f(this)[e](g,h,i,j)}}(c,s)
case 5:return function(e,f){return function(g,h,i,j,k){return f(this)[e](g,h,i,j,k)}}(c,s)
default:return function(e,f){return function(){return e.apply(f(this),arguments)}}(d,s)}},
dh(a,b,c,d){if(c)return A.em(a,b,d)
return A.ek(b.length,d,a,b)},
el(a,b,c,d){var s=A.dg,r=A.ei
switch(b?-1:a){case 0:throw A.b(new A.bj("Intercepted function with no arguments."))
case 1:return function(e,f,g){return function(){return f(this)[e](g(this))}}(c,r,s)
case 2:return function(e,f,g){return function(h){return f(this)[e](g(this),h)}}(c,r,s)
case 3:return function(e,f,g){return function(h,i){return f(this)[e](g(this),h,i)}}(c,r,s)
case 4:return function(e,f,g){return function(h,i,j){return f(this)[e](g(this),h,i,j)}}(c,r,s)
case 5:return function(e,f,g){return function(h,i,j,k){return f(this)[e](g(this),h,i,j,k)}}(c,r,s)
case 6:return function(e,f,g){return function(h,i,j,k,l){return f(this)[e](g(this),h,i,j,k,l)}}(c,r,s)
default:return function(e,f,g){return function(){var q=[g(this)]
Array.prototype.push.apply(q,arguments)
return e.apply(f(this),q)}}(d,r,s)}},
em(a,b,c){var s,r
if($.de==null)$.de=A.dd("interceptor")
if($.df==null)$.df=A.dd("receiver")
s=b.length
r=A.el(s,c,a,b)
return r},
cY(a){return A.en(a)},
eh(a,b){return A.ct(v.typeUniverse,A.Z(a.a),b)},
dg(a){return a.a},
ei(a){return a.b},
dd(a){var s,r,q,p=new A.b_("receiver","interceptor"),o=Object.getOwnPropertyNames(p)
o.$flags=1
s=o
for(o=s.length,r=0;r<o;++r){q=s[r]
if(p[q]===a)return q}throw A.b(A.aW("Field name "+a+" not found.",null))},
hD(a){throw A.b(new A.bq(a))},
fO(a){return v.getIsolateTag(a)},
fY(a){var s,r,q,p,o,n=$.dY.$1(a),m=$.cD[n]
if(m!=null){Object.defineProperty(a,v.dispatchPropertyName,{value:m,enumerable:false,writable:true,configurable:true})
return m.i}s=$.cH[n]
if(s!=null)return s
r=v.interceptorsByTag[n]
if(r==null){q=$.dT.$2(a,n)
if(q!=null){m=$.cD[q]
if(m!=null){Object.defineProperty(a,v.dispatchPropertyName,{value:m,enumerable:false,writable:true,configurable:true})
return m.i}s=$.cH[q]
if(s!=null)return s
r=v.interceptorsByTag[q]
n=q}}if(r==null)return null
s=r.prototype
p=n[0]
if(p==="!"){m=A.cK(s)
$.cD[n]=m
Object.defineProperty(a,v.dispatchPropertyName,{value:m,enumerable:false,writable:true,configurable:true})
return m.i}if(p==="~"){$.cH[n]=s
return s}if(p==="-"){o=A.cK(s)
Object.defineProperty(Object.getPrototypeOf(a),v.dispatchPropertyName,{value:o,enumerable:false,writable:true,configurable:true})
return o.i}if(p==="+")return A.e_(a,s)
if(p==="*")throw A.b(A.dr(n))
if(v.leafTags[n]===true){o=A.cK(s)
Object.defineProperty(Object.getPrototypeOf(a),v.dispatchPropertyName,{value:o,enumerable:false,writable:true,configurable:true})
return o.i}else return A.e_(a,s)},
e_(a,b){var s=Object.getPrototypeOf(a)
Object.defineProperty(s,v.dispatchPropertyName,{value:J.d4(b,s,null,null),enumerable:false,writable:true,configurable:true})
return b},
cK(a){return J.d4(a,!1,null,!!a.$iv)},
fZ(a,b,c){var s=b.prototype
if(v.leafTags[a]===true)return A.cK(s)
else return J.d4(s,c,null,null)},
fS(){if(!0===$.d1)return
$.d1=!0
A.fT()},
fT(){var s,r,q,p,o,n,m,l
$.cD=Object.create(null)
$.cH=Object.create(null)
A.fR()
s=v.interceptorsByTag
r=Object.getOwnPropertyNames(s)
if(typeof window!="undefined"){window
q=function(){}
for(p=0;p<r.length;++p){o=r[p]
n=$.e1.$1(o)
if(n!=null){m=A.fZ(o,s[o],n)
if(m!=null){Object.defineProperty(n,v.dispatchPropertyName,{value:m,enumerable:false,writable:true,configurable:true})
q.prototype=n}}}}for(p=0;p<r.length;++p){o=r[p]
if(/^[A-Za-z_]/.test(o)){l=s[o]
s["!"+o]=l
s["~"+o]=l
s["-"+o]=l
s["+"+o]=l
s["*"+o]=l}}},
fR(){var s,r,q,p,o,n,m=B.f()
m=A.ae(B.h,A.ae(B.i,A.ae(B.d,A.ae(B.d,A.ae(B.j,A.ae(B.k,A.ae(B.l(B.c),m)))))))
if(typeof dartNativeDispatchHooksTransformer!="undefined"){s=dartNativeDispatchHooksTransformer
if(typeof s=="function")s=[s]
if(Array.isArray(s))for(r=0;r<s.length;++r){q=s[r]
if(typeof q=="function")m=q(m)||m}}p=m.getTag
o=m.getUnknownTag
n=m.prototypeForTag
$.dY=new A.cE(p)
$.dT=new A.cF(o)
$.e1=new A.cG(n)},
ae(a,b){return a(b)||b},
fN(a,b){var s=b.length,r=v.rttc[""+s+";"+a]
if(r==null)return null
if(s===0)return r
if(s===r.length)return r.apply(null,b)
return r(b)},
h1(a){if(/[[\]{}()*+?.\\^$|]/.test(a))return a.replace(/[[\]{}()*+?.\\^$|]/g,"\\$&")
return a},
bW:function bW(a,b,c,d,e,f){var _=this
_.a=a
_.b=b
_.c=c
_.d=d
_.e=e
_.f=f},
au:function au(){},
b6:function b6(a,b,c){this.a=a
this.b=b
this.c=c},
bm:function bm(a){this.a=a},
bQ:function bQ(a){this.a=a},
ah:function ah(a,b){this.a=a
this.b=b},
aL:function aL(a){this.a=a
this.b=null},
T:function T(){},
bF:function bF(){},
bG:function bG(){},
bV:function bV(){},
bS:function bS(){},
b_:function b_(a,b){this.a=a
this.b=b},
bq:function bq(a){this.a=a},
bj:function bj(a){this.a=a},
cE:function cE(a){this.a=a},
cF:function cF(a){this.a=a},
cG:function cG(a){this.a=a},
Y(a,b,c){if(a>>>0!==a||a>=c)throw A.b(A.dV(b,a))},
b8:function b8(){},
as:function as(){},
b9:function b9(){},
a2:function a2(){},
aq:function aq(){},
ar:function ar(){},
ba:function ba(){},
bb:function bb(){},
bc:function bc(){},
bd:function bd(){},
be:function be(){},
bf:function bf(){},
bg:function bg(){},
at:function at(){},
bh:function bh(){},
aG:function aG(){},
aH:function aH(){},
aI:function aI(){},
aJ:function aJ(){},
dk(a,b){var s=b.c
return s==null?b.c=A.cU(a,b.x,!0):s},
cR(a,b){var s=b.c
return s==null?b.c=A.aQ(a,"a_",[b.x]):s},
dl(a){var s=a.w
if(s===6||s===7||s===8)return A.dl(a.x)
return s===12||s===13},
eA(a){return a.as},
dW(a){return A.by(v.typeUniverse,a,!1)},
P(a1,a2,a3,a4){var s,r,q,p,o,n,m,l,k,j,i,h,g,f,e,d,c,b,a,a0=a2.w
switch(a0){case 5:case 1:case 2:case 3:case 4:return a2
case 6:s=a2.x
r=A.P(a1,s,a3,a4)
if(r===s)return a2
return A.dF(a1,r,!0)
case 7:s=a2.x
r=A.P(a1,s,a3,a4)
if(r===s)return a2
return A.cU(a1,r,!0)
case 8:s=a2.x
r=A.P(a1,s,a3,a4)
if(r===s)return a2
return A.dD(a1,r,!0)
case 9:q=a2.y
p=A.ad(a1,q,a3,a4)
if(p===q)return a2
return A.aQ(a1,a2.x,p)
case 10:o=a2.x
n=A.P(a1,o,a3,a4)
m=a2.y
l=A.ad(a1,m,a3,a4)
if(n===o&&l===m)return a2
return A.cS(a1,n,l)
case 11:k=a2.x
j=a2.y
i=A.ad(a1,j,a3,a4)
if(i===j)return a2
return A.dE(a1,k,i)
case 12:h=a2.x
g=A.P(a1,h,a3,a4)
f=a2.y
e=A.fB(a1,f,a3,a4)
if(g===h&&e===f)return a2
return A.dC(a1,g,e)
case 13:d=a2.y
a4+=d.length
c=A.ad(a1,d,a3,a4)
o=a2.x
n=A.P(a1,o,a3,a4)
if(c===d&&n===o)return a2
return A.cT(a1,n,c,!0)
case 14:b=a2.x
if(b<a4)return a2
a=a3[b-a4]
if(a==null)return a2
return a
default:throw A.b(A.aZ("Attempted to substitute unexpected RTI kind "+a0))}},
ad(a,b,c,d){var s,r,q,p,o=b.length,n=A.cu(o)
for(s=!1,r=0;r<o;++r){q=b[r]
p=A.P(a,q,c,d)
if(p!==q)s=!0
n[r]=p}return s?n:b},
fC(a,b,c,d){var s,r,q,p,o,n,m=b.length,l=A.cu(m)
for(s=!1,r=0;r<m;r+=3){q=b[r]
p=b[r+1]
o=b[r+2]
n=A.P(a,o,c,d)
if(n!==o)s=!0
l.splice(r,3,q,p,n)}return s?l:b},
fB(a,b,c,d){var s,r=b.a,q=A.ad(a,r,c,d),p=b.b,o=A.ad(a,p,c,d),n=b.c,m=A.fC(a,n,c,d)
if(q===r&&o===p&&m===n)return b
s=new A.bt()
s.a=q
s.b=o
s.c=m
return s},
bD(a,b){a[v.arrayRti]=b
return a},
cZ(a){var s=a.$S
if(s!=null){if(typeof s=="number")return A.fQ(s)
return a.$S()}return null},
fU(a,b){var s
if(A.dl(b))if(a instanceof A.T){s=A.cZ(a)
if(s!=null)return s}return A.Z(a)},
Z(a){if(a instanceof A.d)return A.aa(a)
if(Array.isArray(a))return A.bz(a)
return A.cV(J.af(a))},
bz(a){var s=a[v.arrayRti],r=t.b
if(s==null)return r
if(s.constructor!==r.constructor)return r
return s},
aa(a){var s=a.$ti
return s!=null?s:A.cV(a)},
cV(a){var s=a.constructor,r=s.$ccache
if(r!=null)return r
return A.fg(a,s)},
fg(a,b){var s=a instanceof A.T?Object.getPrototypeOf(Object.getPrototypeOf(a)).constructor:b,r=A.f0(v.typeUniverse,s.name)
b.$ccache=r
return r},
fQ(a){var s,r=v.types,q=r[a]
if(typeof q=="string"){s=A.by(v.typeUniverse,q,!1)
r[a]=s
return s}return q},
fP(a){return A.B(A.aa(a))},
fA(a){var s=a instanceof A.T?A.cZ(a):null
if(s!=null)return s
if(t.R.b(a))return J.db(a).a
if(Array.isArray(a))return A.bz(a)
return A.Z(a)},
B(a){var s=a.r
return s==null?a.r=A.dI(a):s},
dI(a){var s,r,q=a.as,p=q.replace(/\*/g,"")
if(p===q)return a.r=new A.cs(a)
s=A.by(v.typeUniverse,p,!0)
r=s.r
return r==null?s.r=A.dI(s):r},
z(a){return A.B(A.by(v.typeUniverse,a,!1))},
ff(a){var s,r,q,p,o,n,m=this
if(m===t.K)return A.I(m,a,A.fm)
if(!A.K(m))s=m===t._
else s=!0
if(s)return A.I(m,a,A.fq)
s=m.w
if(s===7)return A.I(m,a,A.fd)
if(s===1)return A.I(m,a,A.dN)
r=s===6?m.x:m
q=r.w
if(q===8)return A.I(m,a,A.fi)
if(r===t.S)p=A.dM
else if(r===t.i||r===t.H)p=A.fl
else if(r===t.N)p=A.fo
else p=r===t.y?A.cy:null
if(p!=null)return A.I(m,a,p)
if(q===9){o=r.x
if(r.y.every(A.fV)){m.f="$i"+o
if(o==="i")return A.I(m,a,A.fk)
return A.I(m,a,A.fp)}}else if(q===11){n=A.fN(r.x,r.y)
return A.I(m,a,n==null?A.dN:n)}return A.I(m,a,A.fb)},
I(a,b,c){a.b=c
return a.b(b)},
fe(a){var s,r=this,q=A.fa
if(!A.K(r))s=r===t._
else s=!0
if(s)q=A.f3
else if(r===t.K)q=A.f2
else{s=A.aU(r)
if(s)q=A.fc}r.a=q
return r.a(a)},
bA(a){var s=a.w,r=!0
if(!A.K(a))if(!(a===t._))if(!(a===t.A))if(s!==7)if(!(s===6&&A.bA(a.x)))r=s===8&&A.bA(a.x)||a===t.P||a===t.T
return r},
fb(a){var s=this
if(a==null)return A.bA(s)
return A.fW(v.typeUniverse,A.fU(a,s),s)},
fd(a){if(a==null)return!0
return this.x.b(a)},
fp(a){var s,r=this
if(a==null)return A.bA(r)
s=r.f
if(a instanceof A.d)return!!a[s]
return!!J.af(a)[s]},
fk(a){var s,r=this
if(a==null)return A.bA(r)
if(typeof a!="object")return!1
if(Array.isArray(a))return!0
s=r.f
if(a instanceof A.d)return!!a[s]
return!!J.af(a)[s]},
fa(a){var s=this
if(a==null){if(A.aU(s))return a}else if(s.b(a))return a
A.dJ(a,s)},
fc(a){var s=this
if(a==null)return a
else if(s.b(a))return a
A.dJ(a,s)},
dJ(a,b){throw A.b(A.eR(A.dt(a,A.t(b,null))))},
dt(a,b){return A.bI(a)+": type '"+A.t(A.fA(a),null)+"' is not a subtype of type '"+b+"'"},
eR(a){return new A.aO("TypeError: "+a)},
r(a,b){return new A.aO("TypeError: "+A.dt(a,b))},
fi(a){var s=this,r=s.w===6?s.x:s
return r.x.b(a)||A.cR(v.typeUniverse,r).b(a)},
fm(a){return a!=null},
f2(a){if(a!=null)return a
throw A.b(A.r(a,"Object"))},
fq(a){return!0},
f3(a){return a},
dN(a){return!1},
cy(a){return!0===a||!1===a},
hm(a){if(!0===a)return!0
if(!1===a)return!1
throw A.b(A.r(a,"bool"))},
ho(a){if(!0===a)return!0
if(!1===a)return!1
if(a==null)return a
throw A.b(A.r(a,"bool"))},
hn(a){if(!0===a)return!0
if(!1===a)return!1
if(a==null)return a
throw A.b(A.r(a,"bool?"))},
hp(a){if(typeof a=="number")return a
throw A.b(A.r(a,"double"))},
hr(a){if(typeof a=="number")return a
if(a==null)return a
throw A.b(A.r(a,"double"))},
hq(a){if(typeof a=="number")return a
if(a==null)return a
throw A.b(A.r(a,"double?"))},
dM(a){return typeof a=="number"&&Math.floor(a)===a},
hs(a){if(typeof a=="number"&&Math.floor(a)===a)return a
throw A.b(A.r(a,"int"))},
hu(a){if(typeof a=="number"&&Math.floor(a)===a)return a
if(a==null)return a
throw A.b(A.r(a,"int"))},
ht(a){if(typeof a=="number"&&Math.floor(a)===a)return a
if(a==null)return a
throw A.b(A.r(a,"int?"))},
fl(a){return typeof a=="number"},
hv(a){if(typeof a=="number")return a
throw A.b(A.r(a,"num"))},
hx(a){if(typeof a=="number")return a
if(a==null)return a
throw A.b(A.r(a,"num"))},
hw(a){if(typeof a=="number")return a
if(a==null)return a
throw A.b(A.r(a,"num?"))},
fo(a){return typeof a=="string"},
hy(a){if(typeof a=="string")return a
throw A.b(A.r(a,"String"))},
hA(a){if(typeof a=="string")return a
if(a==null)return a
throw A.b(A.r(a,"String"))},
hz(a){if(typeof a=="string")return a
if(a==null)return a
throw A.b(A.r(a,"String?"))},
dR(a,b){var s,r,q
for(s="",r="",q=0;q<a.length;++q,r=", ")s+=r+A.t(a[q],b)
return s},
fv(a,b){var s,r,q,p,o,n,m=a.x,l=a.y
if(""===m)return"("+A.dR(l,b)+")"
s=l.length
r=m.split(",")
q=r.length-s
for(p="(",o="",n=0;n<s;++n,o=", "){p+=o
if(q===0)p+="{"
p+=A.t(l[n],b)
if(q>=0)p+=" "+r[q];++q}return p+"})"},
dK(a4,a5,a6){var s,r,q,p,o,n,m,l,k,j,i,h,g,f,e,d,c,b,a,a0,a1,a2=", ",a3=null
if(a6!=null){s=a6.length
if(a5==null)a5=A.bD([],t.s)
else a3=a5.length
r=a5.length
for(q=s;q>0;--q)a5.push("T"+(r+q))
for(p=t.X,o=t._,n="<",m="",q=0;q<s;++q,m=a2){l=a5.length
k=l-1-q
if(!(k>=0))return A.C(a5,k)
n=n+m+a5[k]
j=a6[q]
i=j.w
if(!(i===2||i===3||i===4||i===5||j===p))l=j===o
else l=!0
if(!l)n+=" extends "+A.t(j,a5)}n+=">"}else n=""
p=a4.x
h=a4.y
g=h.a
f=g.length
e=h.b
d=e.length
c=h.c
b=c.length
a=A.t(p,a5)
for(a0="",a1="",q=0;q<f;++q,a1=a2)a0+=a1+A.t(g[q],a5)
if(d>0){a0+=a1+"["
for(a1="",q=0;q<d;++q,a1=a2)a0+=a1+A.t(e[q],a5)
a0+="]"}if(b>0){a0+=a1+"{"
for(a1="",q=0;q<b;q+=3,a1=a2){a0+=a1
if(c[q+1])a0+="required "
a0+=A.t(c[q+2],a5)+" "+c[q]}a0+="}"}if(a3!=null){a5.toString
a5.length=a3}return n+"("+a0+") => "+a},
t(a,b){var s,r,q,p,o,n,m,l=a.w
if(l===5)return"erased"
if(l===2)return"dynamic"
if(l===3)return"void"
if(l===1)return"Never"
if(l===4)return"any"
if(l===6)return A.t(a.x,b)
if(l===7){s=a.x
r=A.t(s,b)
q=s.w
return(q===12||q===13?"("+r+")":r)+"?"}if(l===8)return"FutureOr<"+A.t(a.x,b)+">"
if(l===9){p=A.fD(a.x)
o=a.y
return o.length>0?p+("<"+A.dR(o,b)+">"):p}if(l===11)return A.fv(a,b)
if(l===12)return A.dK(a,b,null)
if(l===13)return A.dK(a.x,b,a.y)
if(l===14){n=a.x
m=b.length
n=m-1-n
if(!(n>=0&&n<m))return A.C(b,n)
return b[n]}return"?"},
fD(a){var s=v.mangledGlobalNames[a]
if(s!=null)return s
return"minified:"+a},
f1(a,b){var s=a.tR[b]
for(;typeof s=="string";)s=a.tR[s]
return s},
f0(a,b){var s,r,q,p,o,n=a.eT,m=n[b]
if(m==null)return A.by(a,b,!1)
else if(typeof m=="number"){s=m
r=A.aR(a,5,"#")
q=A.cu(s)
for(p=0;p<s;++p)q[p]=r
o=A.aQ(a,b,q)
n[b]=o
return o}else return m},
eZ(a,b){return A.dG(a.tR,b)},
eY(a,b){return A.dG(a.eT,b)},
by(a,b,c){var s,r=a.eC,q=r.get(b)
if(q!=null)return q
s=A.dA(A.dy(a,null,b,c))
r.set(b,s)
return s},
ct(a,b,c){var s,r,q=b.z
if(q==null)q=b.z=new Map()
s=q.get(c)
if(s!=null)return s
r=A.dA(A.dy(a,b,c,!0))
q.set(c,r)
return r},
f_(a,b,c){var s,r,q,p=b.Q
if(p==null)p=b.Q=new Map()
s=c.as
r=p.get(s)
if(r!=null)return r
q=A.cS(a,b,c.w===10?c.y:[c])
p.set(s,q)
return q},
H(a,b){b.a=A.fe
b.b=A.ff
return b},
aR(a,b,c){var s,r,q=a.eC.get(c)
if(q!=null)return q
s=new A.x(null,null)
s.w=b
s.as=c
r=A.H(a,s)
a.eC.set(c,r)
return r},
dF(a,b,c){var s,r=b.as+"*",q=a.eC.get(r)
if(q!=null)return q
s=A.eW(a,b,r,c)
a.eC.set(r,s)
return s},
eW(a,b,c,d){var s,r,q
if(d){s=b.w
if(!A.K(b))r=b===t.P||b===t.T||s===7||s===6
else r=!0
if(r)return b}q=new A.x(null,null)
q.w=6
q.x=b
q.as=c
return A.H(a,q)},
cU(a,b,c){var s,r=b.as+"?",q=a.eC.get(r)
if(q!=null)return q
s=A.eV(a,b,r,c)
a.eC.set(r,s)
return s},
eV(a,b,c,d){var s,r,q,p
if(d){s=b.w
r=!0
if(!A.K(b))if(!(b===t.P||b===t.T))if(s!==7)r=s===8&&A.aU(b.x)
if(r)return b
else if(s===1||b===t.A)return t.P
else if(s===6){q=b.x
if(q.w===8&&A.aU(q.x))return q
else return A.dk(a,b)}}p=new A.x(null,null)
p.w=7
p.x=b
p.as=c
return A.H(a,p)},
dD(a,b,c){var s,r=b.as+"/",q=a.eC.get(r)
if(q!=null)return q
s=A.eT(a,b,r,c)
a.eC.set(r,s)
return s},
eT(a,b,c,d){var s,r
if(d){s=b.w
if(A.K(b)||b===t.K||b===t._)return b
else if(s===1)return A.aQ(a,"a_",[b])
else if(b===t.P||b===t.T)return t.W}r=new A.x(null,null)
r.w=8
r.x=b
r.as=c
return A.H(a,r)},
eX(a,b){var s,r,q=""+b+"^",p=a.eC.get(q)
if(p!=null)return p
s=new A.x(null,null)
s.w=14
s.x=b
s.as=q
r=A.H(a,s)
a.eC.set(q,r)
return r},
aP(a){var s,r,q,p=a.length
for(s="",r="",q=0;q<p;++q,r=",")s+=r+a[q].as
return s},
eS(a){var s,r,q,p,o,n=a.length
for(s="",r="",q=0;q<n;q+=3,r=","){p=a[q]
o=a[q+1]?"!":":"
s+=r+p+o+a[q+2].as}return s},
aQ(a,b,c){var s,r,q,p=b
if(c.length>0)p+="<"+A.aP(c)+">"
s=a.eC.get(p)
if(s!=null)return s
r=new A.x(null,null)
r.w=9
r.x=b
r.y=c
if(c.length>0)r.c=c[0]
r.as=p
q=A.H(a,r)
a.eC.set(p,q)
return q},
cS(a,b,c){var s,r,q,p,o,n
if(b.w===10){s=b.x
r=b.y.concat(c)}else{r=c
s=b}q=s.as+(";<"+A.aP(r)+">")
p=a.eC.get(q)
if(p!=null)return p
o=new A.x(null,null)
o.w=10
o.x=s
o.y=r
o.as=q
n=A.H(a,o)
a.eC.set(q,n)
return n},
dE(a,b,c){var s,r,q="+"+(b+"("+A.aP(c)+")"),p=a.eC.get(q)
if(p!=null)return p
s=new A.x(null,null)
s.w=11
s.x=b
s.y=c
s.as=q
r=A.H(a,s)
a.eC.set(q,r)
return r},
dC(a,b,c){var s,r,q,p,o,n=b.as,m=c.a,l=m.length,k=c.b,j=k.length,i=c.c,h=i.length,g="("+A.aP(m)
if(j>0){s=l>0?",":""
g+=s+"["+A.aP(k)+"]"}if(h>0){s=l>0?",":""
g+=s+"{"+A.eS(i)+"}"}r=n+(g+")")
q=a.eC.get(r)
if(q!=null)return q
p=new A.x(null,null)
p.w=12
p.x=b
p.y=c
p.as=r
o=A.H(a,p)
a.eC.set(r,o)
return o},
cT(a,b,c,d){var s,r=b.as+("<"+A.aP(c)+">"),q=a.eC.get(r)
if(q!=null)return q
s=A.eU(a,b,c,r,d)
a.eC.set(r,s)
return s},
eU(a,b,c,d,e){var s,r,q,p,o,n,m,l
if(e){s=c.length
r=A.cu(s)
for(q=0,p=0;p<s;++p){o=c[p]
if(o.w===1){r[p]=o;++q}}if(q>0){n=A.P(a,b,r,0)
m=A.ad(a,c,r,0)
return A.cT(a,n,m,c!==m)}}l=new A.x(null,null)
l.w=13
l.x=b
l.y=c
l.as=d
return A.H(a,l)},
dy(a,b,c,d){return{u:a,e:b,r:c,s:[],p:0,n:d}},
dA(a){var s,r,q,p,o,n,m,l=a.r,k=a.s
for(s=l.length,r=0;r<s;){q=l.charCodeAt(r)
if(q>=48&&q<=57)r=A.eL(r+1,q,l,k)
else if((((q|32)>>>0)-97&65535)<26||q===95||q===36||q===124)r=A.dz(a,r,l,k,!1)
else if(q===46)r=A.dz(a,r,l,k,!0)
else{++r
switch(q){case 44:break
case 58:k.push(!1)
break
case 33:k.push(!0)
break
case 59:k.push(A.O(a.u,a.e,k.pop()))
break
case 94:k.push(A.eX(a.u,k.pop()))
break
case 35:k.push(A.aR(a.u,5,"#"))
break
case 64:k.push(A.aR(a.u,2,"@"))
break
case 126:k.push(A.aR(a.u,3,"~"))
break
case 60:k.push(a.p)
a.p=k.length
break
case 62:A.eN(a,k)
break
case 38:A.eM(a,k)
break
case 42:p=a.u
k.push(A.dF(p,A.O(p,a.e,k.pop()),a.n))
break
case 63:p=a.u
k.push(A.cU(p,A.O(p,a.e,k.pop()),a.n))
break
case 47:p=a.u
k.push(A.dD(p,A.O(p,a.e,k.pop()),a.n))
break
case 40:k.push(-3)
k.push(a.p)
a.p=k.length
break
case 41:A.eK(a,k)
break
case 91:k.push(a.p)
a.p=k.length
break
case 93:o=k.splice(a.p)
A.dB(a.u,a.e,o)
a.p=k.pop()
k.push(o)
k.push(-1)
break
case 123:k.push(a.p)
a.p=k.length
break
case 125:o=k.splice(a.p)
A.eP(a.u,a.e,o)
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
return A.O(a.u,a.e,m)},
eL(a,b,c,d){var s,r,q=b-48
for(s=c.length;a<s;++a){r=c.charCodeAt(a)
if(!(r>=48&&r<=57))break
q=q*10+(r-48)}d.push(q)
return a},
dz(a,b,c,d,e){var s,r,q,p,o,n,m=b+1
for(s=c.length;m<s;++m){r=c.charCodeAt(m)
if(r===46){if(e)break
e=!0}else{if(!((((r|32)>>>0)-97&65535)<26||r===95||r===36||r===124))q=r>=48&&r<=57
else q=!0
if(!q)break}}p=c.substring(b,m)
if(e){s=a.u
o=a.e
if(o.w===10)o=o.x
n=A.f1(s,o.x)[p]
if(n==null)A.bE('No "'+p+'" in "'+A.eA(o)+'"')
d.push(A.ct(s,o,n))}else d.push(p)
return m},
eN(a,b){var s,r=a.u,q=A.dx(a,b),p=b.pop()
if(typeof p=="string")b.push(A.aQ(r,p,q))
else{s=A.O(r,a.e,p)
switch(s.w){case 12:b.push(A.cT(r,s,q,a.n))
break
default:b.push(A.cS(r,s,q))
break}}},
eK(a,b){var s,r,q,p=a.u,o=b.pop(),n=null,m=null
if(typeof o=="number")switch(o){case-1:n=b.pop()
break
case-2:m=b.pop()
break
default:b.push(o)
break}else b.push(o)
s=A.dx(a,b)
o=b.pop()
switch(o){case-3:o=b.pop()
if(n==null)n=p.sEA
if(m==null)m=p.sEA
r=A.O(p,a.e,o)
q=new A.bt()
q.a=s
q.b=n
q.c=m
b.push(A.dC(p,r,q))
return
case-4:b.push(A.dE(p,b.pop(),s))
return
default:throw A.b(A.aZ("Unexpected state under `()`: "+A.o(o)))}},
eM(a,b){var s=b.pop()
if(0===s){b.push(A.aR(a.u,1,"0&"))
return}if(1===s){b.push(A.aR(a.u,4,"1&"))
return}throw A.b(A.aZ("Unexpected extended operation "+A.o(s)))},
dx(a,b){var s=b.splice(a.p)
A.dB(a.u,a.e,s)
a.p=b.pop()
return s},
O(a,b,c){if(typeof c=="string")return A.aQ(a,c,a.sEA)
else if(typeof c=="number"){b.toString
return A.eO(a,b,c)}else return c},
dB(a,b,c){var s,r=c.length
for(s=0;s<r;++s)c[s]=A.O(a,b,c[s])},
eP(a,b,c){var s,r=c.length
for(s=2;s<r;s+=3)c[s]=A.O(a,b,c[s])},
eO(a,b,c){var s,r,q=b.w
if(q===10){if(c===0)return b.x
s=b.y
r=s.length
if(c<=r)return s[c-1]
c-=r
b=b.x
q=b.w}else if(c===0)return b
if(q!==9)throw A.b(A.aZ("Indexed base must be an interface type"))
s=b.y
if(c<=s.length)return s[c-1]
throw A.b(A.aZ("Bad index "+c+" for "+b.h(0)))},
fW(a,b,c){var s,r=b.d
if(r==null)r=b.d=new Map()
s=r.get(c)
if(s==null){s=A.m(a,b,null,c,null,!1)?1:0
r.set(c,s)}if(0===s)return!1
if(1===s)return!0
return!0},
m(a,b,c,d,e,f){var s,r,q,p,o,n,m,l,k,j,i
if(b===d)return!0
if(!A.K(d))s=d===t._
else s=!0
if(s)return!0
r=b.w
if(r===4)return!0
if(A.K(b))return!1
s=b.w
if(s===1)return!0
q=r===14
if(q)if(A.m(a,c[b.x],c,d,e,!1))return!0
p=d.w
s=b===t.P||b===t.T
if(s){if(p===8)return A.m(a,b,c,d.x,e,!1)
return d===t.P||d===t.T||p===7||p===6}if(d===t.K){if(r===8)return A.m(a,b.x,c,d,e,!1)
if(r===6)return A.m(a,b.x,c,d,e,!1)
return r!==7}if(r===6)return A.m(a,b.x,c,d,e,!1)
if(p===6){s=A.dk(a,d)
return A.m(a,b,c,s,e,!1)}if(r===8){if(!A.m(a,b.x,c,d,e,!1))return!1
return A.m(a,A.cR(a,b),c,d,e,!1)}if(r===7){s=A.m(a,t.P,c,d,e,!1)
return s&&A.m(a,b.x,c,d,e,!1)}if(p===8){if(A.m(a,b,c,d.x,e,!1))return!0
return A.m(a,b,c,A.cR(a,d),e,!1)}if(p===7){s=A.m(a,b,c,t.P,e,!1)
return s||A.m(a,b,c,d.x,e,!1)}if(q)return!1
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
if(!A.m(a,j,c,i,e,!1)||!A.m(a,i,e,j,c,!1))return!1}return A.dL(a,b.x,c,d.x,e,!1)}if(p===12){if(b===t.g)return!0
if(s)return!1
return A.dL(a,b,c,d,e,!1)}if(r===9){if(p!==9)return!1
return A.fj(a,b,c,d,e,!1)}if(o&&p===11)return A.fn(a,b,c,d,e,!1)
return!1},
dL(a3,a4,a5,a6,a7,a8){var s,r,q,p,o,n,m,l,k,j,i,h,g,f,e,d,c,b,a,a0,a1,a2
if(!A.m(a3,a4.x,a5,a6.x,a7,!1))return!1
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
if(!A.m(a3,p[h],a7,g,a5,!1))return!1}for(h=0;h<m;++h){g=l[h]
if(!A.m(a3,p[o+h],a7,g,a5,!1))return!1}for(h=0;h<i;++h){g=l[m+h]
if(!A.m(a3,k[h],a7,g,a5,!1))return!1}f=s.c
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
if(!A.m(a3,e[a+2],a7,g,a5,!1))return!1
break}}for(;b<d;){if(f[b+1])return!1
b+=3}return!0},
fj(a,b,c,d,e,f){var s,r,q,p,o,n=b.x,m=d.x
for(;n!==m;){s=a.tR[n]
if(s==null)return!1
if(typeof s=="string"){n=s
continue}r=s[m]
if(r==null)return!1
q=r.length
p=q>0?new Array(q):v.typeUniverse.sEA
for(o=0;o<q;++o)p[o]=A.ct(a,b,r[o])
return A.dH(a,p,null,c,d.y,e,!1)}return A.dH(a,b.y,null,c,d.y,e,!1)},
dH(a,b,c,d,e,f,g){var s,r=b.length
for(s=0;s<r;++s)if(!A.m(a,b[s],d,e[s],f,!1))return!1
return!0},
fn(a,b,c,d,e,f){var s,r=b.y,q=d.y,p=r.length
if(p!==q.length)return!1
if(b.x!==d.x)return!1
for(s=0;s<p;++s)if(!A.m(a,r[s],c,q[s],e,!1))return!1
return!0},
aU(a){var s=a.w,r=!0
if(!(a===t.P||a===t.T))if(!A.K(a))if(s!==7)if(!(s===6&&A.aU(a.x)))r=s===8&&A.aU(a.x)
return r},
fV(a){var s
if(!A.K(a))s=a===t._
else s=!0
return s},
K(a){var s=a.w
return s===2||s===3||s===4||s===5||a===t.X},
dG(a,b){var s,r,q=Object.keys(b),p=q.length
for(s=0;s<p;++s){r=q[s]
a[r]=b[r]}},
cu(a){return a>0?new Array(a):v.typeUniverse.sEA},
x:function x(a,b){var _=this
_.a=a
_.b=b
_.r=_.f=_.d=_.c=null
_.w=0
_.as=_.Q=_.z=_.y=_.x=null},
bt:function bt(){this.c=this.b=this.a=null},
cs:function cs(a){this.a=a},
bs:function bs(){},
aO:function aO(a){this.a=a},
eD(){var s,r,q={}
if(self.scheduleImmediate!=null)return A.fG()
if(self.MutationObserver!=null&&self.document!=null){s=self.document.createElement("div")
r=self.document.createElement("span")
q.a=null
new self.MutationObserver(A.cC(new A.c4(q),1)).observe(s,{childList:true})
return new A.c3(q,s,r)}else if(self.setImmediate!=null)return A.fH()
return A.fI()},
eE(a){self.scheduleImmediate(A.cC(new A.c5(a),0))},
eF(a){self.setImmediate(A.cC(new A.c6(a),0))},
eG(a){A.eQ(0,a)},
eQ(a,b){var s=new A.cq()
s.al(a,b)
return s},
fs(a){return new A.bn(new A.p($.k,a.i("p<0>")),a.i("bn<0>"))},
f6(a,b){a.$2(0,null)
b.b=!0
return b.a},
hB(a,b){A.f7(a,b)},
f5(a,b){var s,r=a==null?b.$ti.c.a(a):a
if(!b.b)b.a.a2(r)
else{s=b.a
if(b.$ti.i("a_<1>").b(r))s.a4(r)
else s.L(r)}},
f4(a,b){var s=A.R(a),r=A.J(a),q=b.a
if(b.b)q.v(s,r)
else q.K(s,r)},
f7(a,b){var s,r,q=new A.cw(b),p=new A.cx(b)
if(a instanceof A.p)a.ac(q,p,t.z)
else{s=t.z
if(a instanceof A.p)a.Z(q,p,s)
else{r=new A.p($.k,t.d)
r.a=8
r.c=a
r.ac(q,p,s)}}},
fF(a){var s=function(b,c){return function(d,e){while(true){try{b(d,e)
break}catch(r){e=r
d=c}}}}(a,1)
return $.k.X(new A.cA(s))},
cM(a){var s
if(t.Q.b(a)){s=a.gH()
if(s!=null)return s}return B.m},
du(a,b){var s,r
for(;s=a.a,(s&4)!==0;)a=a.c
if(a===b){b.K(new A.A(!0,a,null,"Cannot complete a future with itself"),A.dm())
return}s|=b.a&1
a.a=s
if((s&24)!==0){r=b.C()
b.B(a)
A.a9(b,r)}else{r=b.c
b.aa(a)
a.T(r)}},
eI(a,b){var s,r,q={},p=q.a=a
for(;s=p.a,(s&4)!==0;){p=p.c
q.a=p}if(p===b){b.K(new A.A(!0,p,null,"Cannot complete a future with itself"),A.dm())
return}if((s&24)===0){r=b.c
b.aa(p)
q.a.T(r)
return}if((s&16)===0&&b.c==null){b.B(p)
return}b.a^=2
A.ac(null,null,b.b,new A.cb(q,b))},
a9(a,b){var s,r,q,p,o,n,m,l,k,j,i,h,g={},f=g.a=a
for(;!0;){s={}
r=f.a
q=(r&16)===0
p=!q
if(b==null){if(p&&(r&1)===0){f=f.c
A.bB(f.a,f.b)}return}s.a=b
o=b.a
for(f=b;o!=null;f=o,o=n){f.a=null
A.a9(g.a,f)
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
if(r){A.bB(m.a,m.b)
return}j=$.k
if(j!==k)$.k=k
else j=null
f=f.c
if((f&15)===8)new A.ci(s,g,p).$0()
else if(q){if((f&1)!==0)new A.ch(s,m).$0()}else if((f&2)!==0)new A.cg(g,s).$0()
if(j!=null)$.k=j
f=s.c
if(f instanceof A.p){r=s.a.$ti
r=r.i("a_<2>").b(f)||!r.y[1].b(f)}else r=!1
if(r){i=s.a.b
if((f.a&24)!==0){h=i.c
i.c=null
b=i.D(h)
i.a=f.a&30|i.a&1
i.c=f.c
g.a=f
continue}else A.du(f,i)
return}}i=s.a.b
h=i.c
i.c=null
b=i.D(h)
f=s.b
r=s.c
if(!f){i.a=8
i.c=r}else{i.a=i.a&1|16
i.c=r}g.a=i
f=i}},
fw(a,b){if(t.C.b(a))return b.X(a)
if(t.v.b(a))return a
throw A.b(A.dc(a,"onError",u.c))},
ft(){var s,r
for(s=$.ab;s!=null;s=$.ab){$.aT=null
r=s.b
$.ab=r
if(r==null)$.aS=null
s.a.$0()}},
fz(){$.cW=!0
try{A.ft()}finally{$.aT=null
$.cW=!1
if($.ab!=null)$.d9().$1(A.dU())}},
dS(a){var s=new A.bo(a),r=$.aS
if(r==null){$.ab=$.aS=s
if(!$.cW)$.d9().$1(A.dU())}else $.aS=r.b=s},
fy(a){var s,r,q,p=$.ab
if(p==null){A.dS(a)
$.aT=$.aS
return}s=new A.bo(a)
r=$.aT
if(r==null){s.b=p
$.ab=$.aT=s}else{q=r.b
s.b=q
$.aT=r.b=s
if(q==null)$.aS=s}},
d6(a){var s=null,r=$.k
if(B.a===r){A.ac(s,s,B.a,a)
return}A.ac(s,s,r,r.ae(a))},
ha(a){A.cX(a,"stream",t.K)
return new A.bw()},
bC(a){return},
eH(a,b,c,d,e){var s=$.k,r=e?1:0,q=c!=null?32:0
A.ds(s,c)
return new A.a6(a,b,s,r|q)},
ds(a,b){if(b==null)b=A.fJ()
if(t.f.b(b))return a.X(b)
if(t.u.b(b))return b
throw A.b(A.aW("handleError callback must take either an Object (the error), or both an Object (the error) and a StackTrace.",null))},
fu(a,b){A.bB(a,b)},
bB(a,b){A.fy(new A.cz(a,b))},
dP(a,b,c,d){var s,r=$.k
if(r===c)return d.$0()
$.k=c
s=r
try{r=d.$0()
return r}finally{$.k=s}},
dQ(a,b,c,d,e){var s,r=$.k
if(r===c)return d.$1(e)
$.k=c
s=r
try{r=d.$1(e)
return r}finally{$.k=s}},
fx(a,b,c,d,e,f){var s,r=$.k
if(r===c)return d.$2(e,f)
$.k=c
s=r
try{r=d.$2(e,f)
return r}finally{$.k=s}},
ac(a,b,c,d){if(B.a!==c)d=c.ae(d)
A.dS(d)},
c4:function c4(a){this.a=a},
c3:function c3(a,b,c){this.a=a
this.b=b
this.c=c},
c5:function c5(a){this.a=a},
c6:function c6(a){this.a=a},
cq:function cq(){},
cr:function cr(a,b){this.a=a
this.b=b},
bn:function bn(a,b){this.a=a
this.b=!1
this.$ti=b},
cw:function cw(a){this.a=a},
cx:function cx(a){this.a=a},
cA:function cA(a){this.a=a},
S:function S(a,b){this.a=a
this.b=b},
aA:function aA(a,b){this.a=a
this.$ti=b},
aB:function aB(a,b,c,d){var _=this
_.ay=0
_.CW=_.ch=null
_.w=a
_.a=b
_.d=c
_.e=d
_.r=null},
a5:function a5(){},
aN:function aN(a,b,c){var _=this
_.a=a
_.b=b
_.c=0
_.e=_.d=null
_.$ti=c},
cp:function cp(a,b){this.a=a
this.b=b},
a8:function a8(a,b,c,d,e){var _=this
_.a=null
_.b=a
_.c=b
_.d=c
_.e=d
_.$ti=e},
p:function p(a,b){var _=this
_.a=0
_.b=a
_.c=null
_.$ti=b},
c8:function c8(a,b){this.a=a
this.b=b},
cf:function cf(a,b){this.a=a
this.b=b},
cc:function cc(a){this.a=a},
cd:function cd(a){this.a=a},
ce:function ce(a,b,c){this.a=a
this.b=b
this.c=c},
cb:function cb(a,b){this.a=a
this.b=b},
ca:function ca(a,b){this.a=a
this.b=b},
c9:function c9(a,b,c){this.a=a
this.b=b
this.c=c},
ci:function ci(a,b,c){this.a=a
this.b=b
this.c=c},
cj:function cj(a){this.a=a},
ch:function ch(a,b){this.a=a
this.b=b},
cg:function cg(a,b){this.a=a
this.b=b},
bo:function bo(a){this.a=a
this.b=null},
a3:function a3(){},
bT:function bT(a,b){this.a=a
this.b=b},
bU:function bU(a,b){this.a=a
this.b=b},
bv:function bv(){},
co:function co(a){this.a=a},
bp:function bp(){},
a4:function a4(a,b,c,d){var _=this
_.a=null
_.b=0
_.d=a
_.e=b
_.f=c
_.$ti=d},
X:function X(a,b){this.a=a
this.$ti=b},
a6:function a6(a,b,c,d){var _=this
_.w=a
_.a=b
_.d=c
_.e=d
_.r=null},
W:function W(){},
aM:function aM(){},
br:function br(){},
a7:function a7(a){this.b=a
this.a=null},
aK:function aK(){this.a=0
this.c=this.b=null},
cl:function cl(a,b){this.a=a
this.b=b},
aC:function aC(a){this.a=1
this.b=a
this.c=null},
bw:function bw(){},
cv:function cv(){},
cz:function cz(a,b){this.a=a
this.b=b},
cm:function cm(){},
cn:function cn(a,b){this.a=a
this.b=b},
dv(a,b){var s=a[b]
return s===a?null:s},
dw(a,b,c){if(c==null)a[b]=a
else a[b]=c},
eJ(){var s=Object.create(null)
A.dw(s,"<non-identifier-key>",s)
delete s["<non-identifier-key>"]
return s},
eu(a){var s,r={}
if(A.d2(a))return"{...}"
s=new A.bk("")
try{$.w.push(a)
s.a+="{"
r.a=!0
a.af(0,new A.bP(r,s))
s.a+="}"}finally{if(0>=$.w.length)return A.C($.w,-1)
$.w.pop()}r=s.a
return r.charCodeAt(0)==0?r:r},
aD:function aD(){},
aF:function aF(a){var _=this
_.a=0
_.e=_.d=_.c=_.b=null
_.$ti=a},
aE:function aE(a,b){this.a=a
this.$ti=b},
bu:function bu(a,b,c){var _=this
_.a=a
_.b=b
_.c=0
_.d=null
_.$ti=c},
j:function j(){},
a1:function a1(){},
bP:function bP(a,b){this.a=a
this.b=b},
eo(a,b){a=A.b(a)
a.stack=b.h(0)
throw a
throw A.b("unreachable")},
et(a,b,c){var s,r
if(a>4294967295)A.bE(A.ez(a,0,4294967295,"length",null))
s=A.bD(new Array(a),c.i("u<0>"))
s.$flags=1
r=s
return r},
dp(a,b,c){var s=J.ef(b)
if(!s.l())return a
if(c.length===0){do a+=A.o(s.gm())
while(s.l())}else{a+=A.o(s.gm())
for(;s.l();)a=a+c+A.o(s.gm())}return a},
dm(){return A.J(new Error())},
bI(a){if(typeof a=="number"||A.cy(a)||a==null)return J.aV(a)
if(typeof a=="string")return JSON.stringify(a)
return A.ey(a)},
ep(a,b){A.cX(a,"error",t.K)
A.cX(b,"stackTrace",t.l)
A.eo(a,b)},
aZ(a){return new A.aY(a)},
aW(a,b){return new A.A(!1,null,b,a)},
dc(a,b,c){return new A.A(!0,a,b,c)},
ez(a,b,c,d,e){return new A.aw(b,c,!0,a,d,"Invalid value")},
eq(a,b,c,d){return new A.b1(b,!0,a,d,"Index out of range")},
eB(a){return new A.az(a)},
dr(a){return new A.bl(a)},
dn(a){return new A.E(a)},
bH(a){return new A.b0(a)},
er(a,b,c){var s,r
if(A.d2(a)){if(b==="("&&c===")")return"(...)"
return b+"..."+c}s=A.bD([],t.s)
$.w.push(a)
try{A.fr(a,s)}finally{if(0>=$.w.length)return A.C($.w,-1)
$.w.pop()}r=A.dp(b,s,", ")+c
return r.charCodeAt(0)==0?r:r},
di(a,b,c){var s,r
if(A.d2(a))return b+"..."+c
s=new A.bk(b)
$.w.push(a)
try{r=s
r.a=A.dp(r.a,a,", ")}finally{if(0>=$.w.length)return A.C($.w,-1)
$.w.pop()}s.a+=c
r=s.a
return r.charCodeAt(0)==0?r:r},
fr(a,b){var s,r,q,p,o,n,m,l=a.gq(a),k=0,j=0
while(!0){if(!(k<80||j<3))break
if(!l.l())return
s=A.o(l.gm())
b.push(s)
k+=s.length+2;++j}if(!l.l()){if(j<=5)return
if(0>=b.length)return A.C(b,-1)
r=b.pop()
if(0>=b.length)return A.C(b,-1)
q=b.pop()}else{p=l.gm();++j
if(!l.l()){if(j<=4){b.push(A.o(p))
return}r=A.o(p)
if(0>=b.length)return A.C(b,-1)
q=b.pop()
k+=r.length+2}else{o=l.gm();++j
for(;l.l();p=o,o=n){n=l.gm();++j
if(j>100){while(!0){if(!(k>75&&j>3))break
if(0>=b.length)return A.C(b,-1)
k-=b.pop().length+2;--j}b.push("...")
return}}q=A.o(p)
r=A.o(o)
k+=r.length+q.length+4}}if(j>b.length+2){k+=5
m="..."}else m=null
while(!0){if(!(k>80&&b.length>3))break
if(0>=b.length)return A.C(b,-1)
k-=b.pop().length+2
if(m==null){k+=5
m="..."}}if(m!=null)b.push(m)
b.push(q)
b.push(r)},
e0(a){A.h0(a)},
l:function l(){},
aY:function aY(a){this.a=a},
F:function F(){},
A:function A(a,b,c,d){var _=this
_.a=a
_.b=b
_.c=c
_.d=d},
aw:function aw(a,b,c,d,e,f){var _=this
_.e=a
_.f=b
_.a=c
_.b=d
_.c=e
_.d=f},
b1:function b1(a,b,c,d,e){var _=this
_.f=a
_.a=b
_.b=c
_.c=d
_.d=e},
az:function az(a){this.a=a},
bl:function bl(a){this.a=a},
E:function E(a){this.a=a},
b0:function b0(a){this.a=a},
ax:function ax(){},
c7:function c7(a){this.a=a},
c:function c(){},
q:function q(){},
d:function d(){},
bx:function bx(){},
bk:function bk(a){this.a=a},
f8(a,b,c){if(c>=1)return a.$1(b)
return a.$0()},
dO(a){return a==null||A.cy(a)||typeof a=="number"||typeof a=="string"||t.U.b(a)||t.E.b(a)||t.e.b(a)||t.O.b(a)||t.D.b(a)||t.k.b(a)||t.w.b(a)||t.B.b(a)||t.q.b(a)||t.J.b(a)||t.Y.b(a)},
fX(a){if(A.dO(a))return a
return new A.cI(new A.aF(t.F)).$1(a)},
cI:function cI(a){this.a=a},
fL(a,b,c,d,e){var s,r=e.i("aN<0>"),q=new A.aN(null,null,r),p=new A.cB(q,c,d)
if(typeof p=="function")A.bE(A.aW("Attempting to rewrap a JS function.",null))
s=function(f,g){return function(h){return f(g,h,arguments.length)}}(A.f8,p)
s[$.d8()]=p
a[b]=s
return new A.aA(q,r.i("aA<1>"))},
eC(){var s=new A.c1()
s.ak()
return s},
d3(){var s=0,r=A.fs(t.n),q,p
var $async$d3=A.fF(function(a,b){if(a===1)return A.f4(b,r)
while(true)switch(s){case 0:q=A.eC()
p=q.a
p===$&&A.e2()
new A.X(p,A.aa(p).i("X<1>")).aJ(new A.cJ(q))
return A.f5(null,r)}})
return A.f6($async$d3,r)},
cB:function cB(a,b,c){this.a=a
this.b=b
this.c=c},
c1:function c1(){this.a=$},
c2:function c2(a){this.a=a},
cJ:function cJ(a){this.a=a},
h0(a){if(typeof dartPrint=="function"){dartPrint(a)
return}if(typeof console=="object"&&typeof console.log!="undefined"){console.log(a)
return}if(typeof print=="function"){print(a)
return}throw"Unable to print message: "+String(a)},
h3(a){A.d7(new A.ap("Field '"+a+"' has been assigned during initialization."),new Error())},
e2(){A.d7(new A.ap("Field '' has not been initialized."),new Error())},
es(a,b,c,d,e,f){var s
if(c==null)return a[b]()
else{s=a[b](c)
return s}}},B={}
var w=[A,J,B]
var $={}
A.cP.prototype={}
J.b2.prototype={
gn(a){return A.av(a)},
h(a){return"Instance of '"+A.bR(a)+"'"},
gj(a){return A.B(A.cV(this))}}
J.b3.prototype={
h(a){return String(a)},
gn(a){return a?519018:218159},
gj(a){return A.B(t.y)},
$if:1}
J.ak.prototype={
h(a){return"null"},
gn(a){return 0},
gj(a){return A.B(t.P)},
$if:1,
$iq:1}
J.an.prototype={$in:1}
J.M.prototype={
gn(a){return 0},
gj(a){return B.A},
h(a){return String(a)}}
J.bi.prototype={}
J.ay.prototype={}
J.L.prototype={
h(a){var s=a[$.d8()]
if(s==null)return this.ai(a)
return"JavaScript function for "+J.aV(s)}}
J.am.prototype={
gn(a){return 0},
h(a){return String(a)}}
J.ao.prototype={
gn(a){return 0},
h(a){return String(a)}}
J.u.prototype={
aG(a,b){var s
a.$flags&1&&A.h4(a,"addAll",2)
for(s=b.gq(b);s.l();)a.push(s.gm())},
F(a,b,c){return new A.D(a,b,A.bz(a).i("@<1>").u(c).i("D<1,2>"))},
E(a,b){if(!(b<a.length))return A.C(a,b)
return a[b]},
h(a){return A.di(a,"[","]")},
gq(a){return new J.aX(a,a.length,A.bz(a).i("aX<1>"))},
gn(a){return A.av(a)},
gk(a){return a.length},
gj(a){return A.B(A.bz(a))},
$ie:1,
$ic:1,
$ii:1}
J.bO.prototype={}
J.aX.prototype={
gm(){var s=this.d
return s==null?this.$ti.c.a(s):s},
l(){var s,r=this,q=r.a,p=q.length
if(r.b!==p)throw A.b(A.h2(q))
s=r.c
if(s>=p){r.d=null
return!1}r.d=q[s]
r.c=s+1
return!0}}
J.b5.prototype={
h(a){if(a===0&&1/a<0)return"-0.0"
else return""+a},
gn(a){var s,r,q,p,o=a|0
if(a===o)return o&536870911
s=Math.abs(a)
r=Math.log(s)/0.6931471805599453|0
q=Math.pow(2,r)
p=s<1?s/q:q/s
return((p*9007199254740992|0)+(p*3542243181176521|0))*599197+r*1259&536870911},
aE(a,b){var s
if(a>0)s=this.aD(a,b)
else{s=b>31?31:b
s=a>>s>>>0}return s},
aD(a,b){return b>31?0:a>>>b},
gj(a){return A.B(t.H)},
$ih:1}
J.aj.prototype={
gj(a){return A.B(t.S)},
$if:1,
$ia:1}
J.b4.prototype={
gj(a){return A.B(t.i)},
$if:1}
J.al.prototype={
h(a){return a},
gn(a){var s,r,q
for(s=a.length,r=0,q=0;q<s;++q){r=r+a.charCodeAt(q)&536870911
r=r+((r&524287)<<10)&536870911
r^=r>>6}r=r+((r&67108863)<<3)&536870911
r^=r>>11
return r+((r&16383)<<15)&536870911},
gj(a){return A.B(t.N)},
gk(a){return a.length},
$if:1,
$iV:1}
A.ap.prototype={
h(a){return"LateInitializationError: "+this.a}}
A.e.prototype={}
A.N.prototype={
gq(a){return new A.a0(this,this.gk(0),this.$ti.i("a0<N.E>"))},
F(a,b,c){return new A.D(this,b,this.$ti.i("@<N.E>").u(c).i("D<1,2>"))}}
A.a0.prototype={
gm(){var s=this.d
return s==null?this.$ti.c.a(s):s},
l(){var s,r=this,q=r.a,p=J.dX(q),o=p.gk(q)
if(r.b!==o)throw A.b(A.bH(q))
s=r.c
if(s>=o){r.d=null
return!1}r.d=p.E(q,s);++r.c
return!0}}
A.U.prototype={
gq(a){var s=this.a
return new A.b7(s.gq(s),this.b,A.aa(this).i("b7<1,2>"))},
gk(a){var s=this.a
return s.gk(s)}}
A.ag.prototype={$ie:1}
A.b7.prototype={
l(){var s=this,r=s.b
if(r.l()){s.a=s.c.$1(r.gm())
return!0}s.a=null
return!1},
gm(){var s=this.a
return s==null?this.$ti.y[1].a(s):s}}
A.D.prototype={
gk(a){return J.cL(this.a)},
E(a,b){return this.b.$1(J.ee(this.a,b))}}
A.ai.prototype={}
A.bW.prototype={
t(a){var s,r,q=this,p=new RegExp(q.a).exec(a)
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
A.au.prototype={
h(a){return"Null check operator used on a null value"}}
A.b6.prototype={
h(a){var s,r=this,q="NoSuchMethodError: method not found: '",p=r.b
if(p==null)return"NoSuchMethodError: "+r.a
s=r.c
if(s==null)return q+p+"' ("+r.a+")"
return q+p+"' on '"+s+"' ("+r.a+")"}}
A.bm.prototype={
h(a){var s=this.a
return s.length===0?"Error":"Error: "+s}}
A.bQ.prototype={
h(a){return"Throw of null ('"+(this.a===null?"null":"undefined")+"' from JavaScript)"}}
A.ah.prototype={}
A.aL.prototype={
h(a){var s,r=this.b
if(r!=null)return r
r=this.a
s=r!==null&&typeof r==="object"?r.stack:null
return this.b=s==null?"":s},
$iy:1}
A.T.prototype={
h(a){var s=this.constructor,r=s==null?null:s.name
return"Closure '"+A.e3(r==null?"unknown":r)+"'"},
gj(a){var s=A.cZ(this)
return A.B(s==null?A.Z(this):s)},
gaV(){return this},
$C:"$1",
$R:1,
$D:null}
A.bF.prototype={$C:"$0",$R:0}
A.bG.prototype={$C:"$2",$R:2}
A.bV.prototype={}
A.bS.prototype={
h(a){var s=this.$static_name
if(s==null)return"Closure of unknown static method"
return"Closure '"+A.e3(s)+"'"}}
A.b_.prototype={
gn(a){return(A.d5(this.a)^A.av(this.$_target))>>>0},
h(a){return"Closure '"+this.$_name+"' of "+("Instance of '"+A.bR(this.a)+"'")}}
A.bq.prototype={
h(a){return"Reading static variable '"+this.a+"' during its initialization"}}
A.bj.prototype={
h(a){return"RuntimeError: "+this.a}}
A.cE.prototype={
$1(a){return this.a(a)},
$S:6}
A.cF.prototype={
$2(a,b){return this.a(a,b)},
$S:7}
A.cG.prototype={
$1(a){return this.a(a)},
$S:8}
A.b8.prototype={
gj(a){return B.t},
$if:1,
$icN:1}
A.as.prototype={}
A.b9.prototype={
gj(a){return B.u},
$if:1,
$icO:1}
A.a2.prototype={
gk(a){return a.length},
$iv:1}
A.aq.prototype={
p(a,b){A.Y(b,a,a.length)
return a[b]},
$ie:1,
$ic:1,
$ii:1}
A.ar.prototype={$ie:1,$ic:1,$ii:1}
A.ba.prototype={
gj(a){return B.v},
$if:1,
$ibJ:1}
A.bb.prototype={
gj(a){return B.w},
$if:1,
$ibK:1}
A.bc.prototype={
gj(a){return B.x},
p(a,b){A.Y(b,a,a.length)
return a[b]},
$if:1,
$ibL:1}
A.bd.prototype={
gj(a){return B.y},
p(a,b){A.Y(b,a,a.length)
return a[b]},
$if:1,
$ibM:1}
A.be.prototype={
gj(a){return B.z},
p(a,b){A.Y(b,a,a.length)
return a[b]},
$if:1,
$ibN:1}
A.bf.prototype={
gj(a){return B.B},
p(a,b){A.Y(b,a,a.length)
return a[b]},
$if:1,
$ibY:1}
A.bg.prototype={
gj(a){return B.C},
p(a,b){A.Y(b,a,a.length)
return a[b]},
$if:1,
$ibZ:1}
A.at.prototype={
gj(a){return B.D},
gk(a){return a.length},
p(a,b){A.Y(b,a,a.length)
return a[b]},
$if:1,
$ic_:1}
A.bh.prototype={
gj(a){return B.E},
gk(a){return a.length},
p(a,b){A.Y(b,a,a.length)
return a[b]},
$if:1,
$ic0:1}
A.aG.prototype={}
A.aH.prototype={}
A.aI.prototype={}
A.aJ.prototype={}
A.x.prototype={
i(a){return A.ct(v.typeUniverse,this,a)},
u(a){return A.f_(v.typeUniverse,this,a)}}
A.bt.prototype={}
A.cs.prototype={
h(a){return A.t(this.a,null)}}
A.bs.prototype={
h(a){return this.a}}
A.aO.prototype={$iF:1}
A.c4.prototype={
$1(a){var s=this.a,r=s.a
s.a=null
r.$0()},
$S:2}
A.c3.prototype={
$1(a){var s,r
this.a.a=a
s=this.b
r=this.c
s.firstChild?s.removeChild(r):s.appendChild(r)},
$S:9}
A.c5.prototype={
$0(){this.a.$0()},
$S:3}
A.c6.prototype={
$0(){this.a.$0()},
$S:3}
A.cq.prototype={
al(a,b){if(self.setTimeout!=null)self.setTimeout(A.cC(new A.cr(this,b),0),a)
else throw A.b(A.eB("`setTimeout()` not found."))}}
A.cr.prototype={
$0(){this.b.$0()},
$S:0}
A.bn.prototype={}
A.cw.prototype={
$1(a){return this.a.$2(0,a)},
$S:4}
A.cx.prototype={
$2(a,b){this.a.$2(1,new A.ah(a,b))},
$S:10}
A.cA.prototype={
$2(a,b){this.a(a,b)},
$S:11}
A.S.prototype={
h(a){return A.o(this.a)},
$il:1,
gH(){return this.b}}
A.aA.prototype={}
A.aB.prototype={
R(){},
S(){}}
A.a5.prototype={
gP(){return this.c<4},
ab(a,b,c,d){var s,r,q,p,o,n=this
if((n.c&4)!==0){s=new A.aC($.k)
A.d6(s.gaw())
if(c!=null)s.c=c
return s}s=$.k
r=d?1:0
q=b!=null?32:0
A.ds(s,b)
p=new A.aB(n,a,s,r|q)
p.CW=p
p.ch=p
p.ay=n.c&1
o=n.e
n.e=p
p.ch=null
p.CW=o
if(o==null)n.d=p
else o.ch=p
if(n.d===p)A.bC(n.a)
return p},
a8(a){},
a9(a){},
I(){if((this.c&4)!==0)return new A.E("Cannot add new events after calling close")
return new A.E("Cannot add new events while doing an addStream")},
au(a){var s,r,q,p,o=this,n=o.c
if((n&2)!==0)throw A.b(A.dn(u.g))
s=o.d
if(s==null)return
r=n&1
o.c=n^3
for(;s!=null;){n=s.ay
if((n&1)===r){s.ay=n|2
a.$1(s)
n=s.ay^=1
q=s.ch
if((n&4)!==0){p=s.CW
if(p==null)o.d=q
else p.ch=q
if(q==null)o.e=p
else q.CW=p
s.CW=s
s.ch=s}s.ay=n&4294967293
s=q}else s=s.ch}o.c&=4294967293
if(o.d==null)o.a3()},
a3(){if((this.c&4)!==0)if(null.gaW())null.a2(null)
A.bC(this.b)}}
A.aN.prototype={
gP(){return A.a5.prototype.gP.call(this)&&(this.c&2)===0},
I(){if((this.c&2)!==0)return new A.E(u.g)
return this.aj()},
A(a){var s=this,r=s.d
if(r==null)return
if(r===s.e){s.c|=2
r.a0(a)
s.c&=4294967293
if(s.d==null)s.a3()
return}s.au(new A.cp(s,a))}}
A.cp.prototype={
$1(a){a.a0(this.b)},
$S(){return this.a.$ti.i("~(W<1>)")}}
A.a8.prototype={
aK(a){if((this.c&15)!==6)return!0
return this.b.b.Y(this.d,a.a)},
aI(a){var s,r=this.e,q=null,p=a.a,o=this.b.b
if(t.C.b(r))q=o.aP(r,p,a.b)
else q=o.Y(r,p)
try{p=q
return p}catch(s){if(t.c.b(A.R(s))){if((this.c&1)!==0)throw A.b(A.aW("The error handler of Future.then must return a value of the returned future's type","onError"))
throw A.b(A.aW("The error handler of Future.catchError must return a value of the future's type","onError"))}else throw s}}}
A.p.prototype={
aa(a){this.a=this.a&1|4
this.c=a},
Z(a,b,c){var s,r,q=$.k
if(q===B.a){if(b!=null&&!t.C.b(b)&&!t.v.b(b))throw A.b(A.dc(b,"onError",u.c))}else if(b!=null)b=A.fw(b,q)
s=new A.p(q,c.i("p<0>"))
r=b==null?1:3
this.J(new A.a8(s,r,a,b,this.$ti.i("@<1>").u(c).i("a8<1,2>")))
return s},
aU(a,b){return this.Z(a,null,b)},
ac(a,b,c){var s=new A.p($.k,c.i("p<0>"))
this.J(new A.a8(s,19,a,b,this.$ti.i("@<1>").u(c).i("a8<1,2>")))
return s},
aB(a){this.a=this.a&1|16
this.c=a},
B(a){this.a=a.a&30|this.a&1
this.c=a.c},
J(a){var s=this,r=s.a
if(r<=3){a.a=s.c
s.c=a}else{if((r&4)!==0){r=s.c
if((r.a&24)===0){r.J(a)
return}s.B(r)}A.ac(null,null,s.b,new A.c8(s,a))}},
T(a){var s,r,q,p,o,n=this,m={}
m.a=a
if(a==null)return
s=n.a
if(s<=3){r=n.c
n.c=a
if(r!=null){q=a.a
for(p=a;q!=null;p=q,q=o)o=q.a
p.a=r}}else{if((s&4)!==0){s=n.c
if((s.a&24)===0){s.T(a)
return}n.B(s)}m.a=n.D(a)
A.ac(null,null,n.b,new A.cf(m,n))}},
C(){var s=this.c
this.c=null
return this.D(s)},
D(a){var s,r,q
for(s=a,r=null;s!=null;r=s,s=q){q=s.a
s.a=r}return r},
ao(a){var s,r,q,p=this
p.a^=2
try{a.Z(new A.cc(p),new A.cd(p),t.P)}catch(q){s=A.R(q)
r=A.J(q)
A.d6(new A.ce(p,s,r))}},
L(a){var s=this,r=s.C()
s.a=8
s.c=a
A.a9(s,r)},
v(a,b){var s=this.C()
this.aB(new A.S(a,b))
A.a9(this,s)},
a2(a){if(this.$ti.i("a_<1>").b(a)){this.a4(a)
return}this.am(a)},
am(a){this.a^=2
A.ac(null,null,this.b,new A.ca(this,a))},
a4(a){if(this.$ti.b(a)){A.eI(a,this)
return}this.ao(a)},
K(a,b){this.a^=2
A.ac(null,null,this.b,new A.c9(this,a,b))},
$ia_:1}
A.c8.prototype={
$0(){A.a9(this.a,this.b)},
$S:0}
A.cf.prototype={
$0(){A.a9(this.b,this.a.a)},
$S:0}
A.cc.prototype={
$1(a){var s,r,q,p=this.a
p.a^=2
try{p.L(p.$ti.c.a(a))}catch(q){s=A.R(q)
r=A.J(q)
p.v(s,r)}},
$S:2}
A.cd.prototype={
$2(a,b){this.a.v(a,b)},
$S:12}
A.ce.prototype={
$0(){this.a.v(this.b,this.c)},
$S:0}
A.cb.prototype={
$0(){A.du(this.a.a,this.b)},
$S:0}
A.ca.prototype={
$0(){this.a.L(this.b)},
$S:0}
A.c9.prototype={
$0(){this.a.v(this.b,this.c)},
$S:0}
A.ci.prototype={
$0(){var s,r,q,p,o,n,m,l=this,k=null
try{q=l.a.a
k=q.b.b.aN(q.d)}catch(p){s=A.R(p)
r=A.J(p)
if(l.c&&l.b.a.c.a===s){q=l.a
q.c=l.b.a.c}else{q=s
o=r
if(o==null)o=A.cM(q)
n=l.a
n.c=new A.S(q,o)
q=n}q.b=!0
return}if(k instanceof A.p&&(k.a&24)!==0){if((k.a&16)!==0){q=l.a
q.c=k.c
q.b=!0}return}if(k instanceof A.p){m=l.b.a
q=l.a
q.c=k.aU(new A.cj(m),t.z)
q.b=!1}},
$S:0}
A.cj.prototype={
$1(a){return this.a},
$S:13}
A.ch.prototype={
$0(){var s,r,q,p,o,n
try{q=this.a
p=q.a
q.c=p.b.b.Y(p.d,this.b)}catch(o){s=A.R(o)
r=A.J(o)
q=s
p=r
if(p==null)p=A.cM(q)
n=this.a
n.c=new A.S(q,p)
n.b=!0}},
$S:0}
A.cg.prototype={
$0(){var s,r,q,p,o,n,m,l=this
try{s=l.a.a.c
p=l.b
if(p.a.aK(s)&&p.a.e!=null){p.c=p.a.aI(s)
p.b=!1}}catch(o){r=A.R(o)
q=A.J(o)
p=l.a.a.c
if(p.a===r){n=l.b
n.c=p
p=n}else{p=r
n=q
if(n==null)n=A.cM(p)
m=l.b
m.c=new A.S(p,n)
p=m}p.b=!0}},
$S:0}
A.bo.prototype={}
A.a3.prototype={
gk(a){var s={},r=new A.p($.k,t.a)
s.a=0
this.ag(new A.bT(s,this),!0,new A.bU(s,r),r.gap())
return r}}
A.bT.prototype={
$1(a){++this.a.a},
$S(){return A.aa(this.b).i("~(1)")}}
A.bU.prototype={
$0(){var s=this.b,r=this.a.a,q=s.C()
s.a=8
s.c=r
A.a9(s,q)},
$S:0}
A.bv.prototype={
gaA(){if((this.b&8)===0)return this.a
return this.a.gU()},
ar(){var s,r=this
if((r.b&8)===0){s=r.a
return s==null?r.a=new A.aK():s}s=r.a.gU()
return s},
gaF(){var s=this.a
return(this.b&8)!==0?s.gU():s},
an(){if((this.b&4)!==0)return new A.E("Cannot add event after closing")
return new A.E("Cannot add event while adding a stream")},
ab(a,b,c,d){var s,r,q,p,o=this
if((o.b&3)!==0)throw A.b(A.dn("Stream has already been listened to."))
s=A.eH(o,a,b,c,d)
r=o.gaA()
q=o.b|=1
if((q&8)!==0){p=o.a
p.sU(s)
p.aM()}else o.a=s
s.aC(r)
q=s.e
s.e=q|64
new A.co(o).$0()
s.e&=4294967231
s.a5((q&4)!==0)
return s},
a8(a){if((this.b&8)!==0)this.a.aX()
A.bC(this.e)},
a9(a){if((this.b&8)!==0)this.a.aM()
A.bC(this.f)}}
A.co.prototype={
$0(){A.bC(this.a.d)},
$S:0}
A.bp.prototype={
A(a){this.gaF().a1(new A.a7(a))}}
A.a4.prototype={}
A.X.prototype={
gn(a){return(A.av(this.a)^892482866)>>>0}}
A.a6.prototype={
R(){this.w.a8(this)},
S(){this.w.a9(this)}}
A.W.prototype={
aC(a){if(a==null)return
this.r=a
if(a.c!=null){this.e|=128
a.G(this)}},
a0(a){var s=this.e
if((s&8)!==0)return
if(s<64)this.A(a)
else this.a1(new A.a7(a))},
R(){},
S(){},
a1(a){var s,r=this,q=r.r
if(q==null)q=r.r=new A.aK()
q.ad(0,a)
s=r.e
if((s&128)===0){s|=128
r.e=s
if(s<256)q.G(r)}},
A(a){var s=this,r=s.e
s.e=r|64
s.d.aT(s.a,a)
s.e&=4294967231
s.a5((r&4)!==0)},
a5(a){var s,r,q=this,p=q.e
if((p&128)!==0&&q.r.c==null){p=q.e=p&4294967167
s=!1
if((p&4)!==0)if(p<256){s=q.r
s=s==null?null:s.c==null
s=s!==!1}if(s){p&=4294967291
q.e=p}}for(;!0;a=r){if((p&8)!==0){q.r=null
return}r=(p&4)!==0
if(a===r)break
q.e=p^64
if(r)q.R()
else q.S()
p=q.e&=4294967231}if((p&128)!==0&&p<256)q.r.G(q)}}
A.aM.prototype={
ag(a,b,c,d){return this.a.ab(a,d,c,b===!0)},
aJ(a){return this.ag(a,null,null,null)}}
A.br.prototype={}
A.a7.prototype={}
A.aK.prototype={
G(a){var s=this,r=s.a
if(r===1)return
if(r>=1){s.a=1
return}A.d6(new A.cl(s,a))
s.a=1},
ad(a,b){var s=this,r=s.c
if(r==null)s.b=s.c=b
else s.c=r.a=b}}
A.cl.prototype={
$0(){var s,r,q=this.a,p=q.a
q.a=0
if(p===3)return
s=q.b
r=s.a
q.b=r
if(r==null)q.c=null
this.b.A(s.b)},
$S:0}
A.aC.prototype={
az(){var s,r=this,q=r.a-1
if(q===0){r.a=-1
s=r.c
if(s!=null){r.c=null
r.b.ah(s)}}else r.a=q}}
A.bw.prototype={}
A.cv.prototype={}
A.cz.prototype={
$0(){A.ep(this.a,this.b)},
$S:0}
A.cm.prototype={
ah(a){var s,r,q
try{if(B.a===$.k){a.$0()
return}A.dP(null,null,this,a)}catch(q){s=A.R(q)
r=A.J(q)
A.bB(s,r)}},
aS(a,b){var s,r,q
try{if(B.a===$.k){a.$1(b)
return}A.dQ(null,null,this,a,b)}catch(q){s=A.R(q)
r=A.J(q)
A.bB(s,r)}},
aT(a,b){return this.aS(a,b,t.z)},
ae(a){return new A.cn(this,a)},
aO(a){if($.k===B.a)return a.$0()
return A.dP(null,null,this,a)},
aN(a){return this.aO(a,t.z)},
aR(a,b){if($.k===B.a)return a.$1(b)
return A.dQ(null,null,this,a,b)},
Y(a,b){var s=t.z
return this.aR(a,b,s,s)},
aQ(a,b,c){if($.k===B.a)return a.$2(b,c)
return A.fx(null,null,this,a,b,c)},
aP(a,b,c){var s=t.z
return this.aQ(a,b,c,s,s,s)},
aL(a){return a},
X(a){var s=t.z
return this.aL(a,s,s,s)}}
A.cn.prototype={
$0(){return this.a.ah(this.b)},
$S:0}
A.aD.prototype={
gk(a){return this.a},
gW(){return new A.aE(this,this.$ti.i("aE<1>"))},
aH(a){var s,r
if(typeof a=="string"&&a!=="__proto__"){s=this.b
return s==null?!1:s[a]!=null}else if(typeof a=="number"&&(a&1073741823)===a){r=this.c
return r==null?!1:r[a]!=null}else return this.aq(a)},
aq(a){var s=this.d
if(s==null)return!1
return this.O(this.a7(s,a),a)>=0},
p(a,b){var s,r,q
if(typeof b=="string"&&b!=="__proto__"){s=this.b
r=s==null?null:A.dv(s,b)
return r}else if(typeof b=="number"&&(b&1073741823)===b){q=this.c
r=q==null?null:A.dv(q,b)
return r}else return this.av(b)},
av(a){var s,r,q=this.d
if(q==null)return null
s=this.a7(q,a)
r=this.O(s,a)
return r<0?null:s[r+1]},
a_(a,b,c){var s,r,q,p=this,o=p.d
if(o==null)o=p.d=A.eJ()
s=A.d5(b)&1073741823
r=o[s]
if(r==null){A.dw(o,s,[b,c]);++p.a
p.e=null}else{q=p.O(r,b)
if(q>=0)r[q+1]=c
else{r.push(b,c);++p.a
p.e=null}}},
af(a,b){var s,r,q,p,o,n=this,m=n.a6()
for(s=m.length,r=n.$ti.y[1],q=0;q<s;++q){p=m[q]
o=n.p(0,p)
b.$2(p,o==null?r.a(o):o)
if(m!==n.e)throw A.b(A.bH(n))}},
a6(){var s,r,q,p,o,n,m,l,k,j,i=this,h=i.e
if(h!=null)return h
h=A.et(i.a,null,t.z)
s=i.b
r=0
if(s!=null){q=Object.getOwnPropertyNames(s)
p=q.length
for(o=0;o<p;++o){h[r]=q[o];++r}}n=i.c
if(n!=null){q=Object.getOwnPropertyNames(n)
p=q.length
for(o=0;o<p;++o){h[r]=+q[o];++r}}m=i.d
if(m!=null){q=Object.getOwnPropertyNames(m)
p=q.length
for(o=0;o<p;++o){l=m[q[o]]
k=l.length
for(j=0;j<k;j+=2){h[r]=l[j];++r}}}return i.e=h},
a7(a,b){return a[A.d5(b)&1073741823]}}
A.aF.prototype={
O(a,b){var s,r,q
if(a==null)return-1
s=a.length
for(r=0;r<s;r+=2){q=a[r]
if(q==null?b==null:q===b)return r}return-1}}
A.aE.prototype={
gk(a){return this.a.a},
gq(a){var s=this.a
return new A.bu(s,s.a6(),this.$ti.i("bu<1>"))}}
A.bu.prototype={
gm(){var s=this.d
return s==null?this.$ti.c.a(s):s},
l(){var s=this,r=s.b,q=s.c,p=s.a
if(r!==p.e)throw A.b(A.bH(p))
else if(q>=r.length){s.d=null
return!1}else{s.d=r[q]
s.c=q+1
return!0}}}
A.j.prototype={
gq(a){return new A.a0(a,this.gk(a),A.Z(a).i("a0<j.E>"))},
E(a,b){return this.p(a,b)},
F(a,b,c){return new A.D(a,b,A.Z(a).i("@<j.E>").u(c).i("D<1,2>"))},
h(a){return A.di(a,"[","]")}}
A.a1.prototype={
af(a,b){var s,r,q,p
for(s=this.gW(),s=s.gq(s),r=A.aa(this).y[1];s.l();){q=s.gm()
p=this.p(0,q)
b.$2(q,p==null?r.a(p):p)}},
gk(a){var s=this.gW()
return s.gk(s)},
h(a){return A.eu(this)}}
A.bP.prototype={
$2(a,b){var s,r=this.a
if(!r.a)this.b.a+=", "
r.a=!1
r=this.b
s=A.o(a)
s=r.a+=s
r.a=s+": "
s=A.o(b)
r.a+=s},
$S:14}
A.l.prototype={
gH(){return A.ex(this)}}
A.aY.prototype={
h(a){var s=this.a
if(s!=null)return"Assertion failed: "+A.bI(s)
return"Assertion failed"}}
A.F.prototype={}
A.A.prototype={
gN(){return"Invalid argument"+(!this.a?"(s)":"")},
gM(){return""},
h(a){var s=this,r=s.c,q=r==null?"":" ("+r+")",p=s.d,o=p==null?"":": "+p,n=s.gN()+q+o
if(!s.a)return n
return n+s.gM()+": "+A.bI(s.gV())},
gV(){return this.b}}
A.aw.prototype={
gV(){return this.b},
gN(){return"RangeError"},
gM(){var s,r=this.e,q=this.f
if(r==null)s=q!=null?": Not less than or equal to "+A.o(q):""
else if(q==null)s=": Not greater than or equal to "+A.o(r)
else if(q>r)s=": Not in inclusive range "+A.o(r)+".."+A.o(q)
else s=q<r?": Valid value range is empty":": Only valid value is "+A.o(r)
return s}}
A.b1.prototype={
gV(){return this.b},
gN(){return"RangeError"},
gM(){if(this.b<0)return": index must not be negative"
var s=this.f
if(s===0)return": no indices are valid"
return": index should be less than "+s},
gk(a){return this.f}}
A.az.prototype={
h(a){return"Unsupported operation: "+this.a}}
A.bl.prototype={
h(a){return"UnimplementedError: "+this.a}}
A.E.prototype={
h(a){return"Bad state: "+this.a}}
A.b0.prototype={
h(a){var s=this.a
if(s==null)return"Concurrent modification during iteration."
return"Concurrent modification during iteration: "+A.bI(s)+"."}}
A.ax.prototype={
h(a){return"Stack Overflow"},
gH(){return null},
$il:1}
A.c7.prototype={
h(a){return"Exception: "+this.a}}
A.c.prototype={
F(a,b,c){return A.ev(this,b,A.aa(this).i("c.E"),c)},
gk(a){var s,r=this.gq(this)
for(s=0;r.l();)++s
return s},
h(a){return A.er(this,"(",")")}}
A.q.prototype={
gn(a){return A.d.prototype.gn.call(this,0)},
h(a){return"null"}}
A.d.prototype={$id:1,
gn(a){return A.av(this)},
h(a){return"Instance of '"+A.bR(this)+"'"},
gj(a){return A.fP(this)},
toString(){return this.h(this)}}
A.bx.prototype={
h(a){return""},
$iy:1}
A.bk.prototype={
gk(a){return this.a.length},
h(a){var s=this.a
return s.charCodeAt(0)==0?s:s}}
A.cI.prototype={
$1(a){var s,r,q,p
if(A.dO(a))return a
s=this.a
if(s.aH(a))return s.p(0,a)
if(a instanceof A.a1){r={}
s.a_(0,a,r)
for(s=a.gW(),s=s.gq(s);s.l();){q=s.gm()
r[q]=this.$1(a.p(0,q))}return r}else if(t.x.b(a)){p=[]
s.a_(0,a,p)
B.o.aG(p,J.eg(a,this,t.z))
return p}else return a},
$S:15}
A.cB.prototype={
$1(a){var s=this.a,r=this.b.$1(this.c.a(a))
if(!s.gP())A.bE(s.I())
s.A(r)},
$S:16}
A.c1.prototype={
ak(){this.a=new A.a4(null,null,null,t.I)
A.fL(self.self,"onmessage",new A.c2(this),t.m,t.P)}}
A.c2.prototype={
$1(a){var s,r=a.data,q=this.a.a
q===$&&A.e2()
s=q.b
if(s>=4)A.bE(q.an())
if((s&1)!==0)q.A(r)
else if((s&3)===0)q.ar().ad(0,new A.a7(r))},
$S:17}
A.cJ.prototype={
$1(a){var s,r
A.e0("Dart worker: onMessage received "+A.o(a)+" with type of "+J.db(a).h(0)+"\n")
try{s=t.m.a(self)
A.es(s,"postMessage",A.fX(a==null?t.K.a(a):a),null,null,null)}catch(r){A.e0("Received data from WASM worker but it's not a String!\n")}},
$S:4};(function aliases(){var s=J.M.prototype
s.ai=s.h
s=A.a5.prototype
s.aj=s.I})();(function installTearOffs(){var s=hunkHelpers._static_1,r=hunkHelpers._static_0,q=hunkHelpers._static_2,p=hunkHelpers._instance_2u,o=hunkHelpers._instance_0u
s(A,"fG","eE",1)
s(A,"fH","eF",1)
s(A,"fI","eG",1)
r(A,"dU","fz",0)
q(A,"fJ","fu",5)
p(A.p.prototype,"gap","v",5)
o(A.aC.prototype,"gaw","az",0)})();(function inheritance(){var s=hunkHelpers.mixin,r=hunkHelpers.inherit,q=hunkHelpers.inheritMany
r(A.d,null)
q(A.d,[A.cP,J.b2,J.aX,A.l,A.c,A.a0,A.b7,A.ai,A.bW,A.bQ,A.ah,A.aL,A.T,A.x,A.bt,A.cs,A.cq,A.bn,A.S,A.a3,A.W,A.a5,A.a8,A.p,A.bo,A.bv,A.bp,A.br,A.aK,A.aC,A.bw,A.cv,A.a1,A.bu,A.j,A.ax,A.c7,A.q,A.bx,A.bk,A.c1])
q(J.b2,[J.b3,J.ak,J.an,J.am,J.ao,J.b5,J.al])
q(J.an,[J.M,J.u,A.b8,A.as])
q(J.M,[J.bi,J.ay,J.L])
r(J.bO,J.u)
q(J.b5,[J.aj,J.b4])
q(A.l,[A.ap,A.F,A.b6,A.bm,A.bq,A.bj,A.bs,A.aY,A.A,A.az,A.bl,A.E,A.b0])
q(A.c,[A.e,A.U])
q(A.e,[A.N,A.aE])
r(A.ag,A.U)
r(A.D,A.N)
r(A.au,A.F)
q(A.T,[A.bF,A.bG,A.bV,A.cE,A.cG,A.c4,A.c3,A.cw,A.cp,A.cc,A.cj,A.bT,A.cI,A.cB,A.c2,A.cJ])
q(A.bV,[A.bS,A.b_])
q(A.bG,[A.cF,A.cx,A.cA,A.cd,A.bP])
q(A.as,[A.b9,A.a2])
q(A.a2,[A.aG,A.aI])
r(A.aH,A.aG)
r(A.aq,A.aH)
r(A.aJ,A.aI)
r(A.ar,A.aJ)
q(A.aq,[A.ba,A.bb])
q(A.ar,[A.bc,A.bd,A.be,A.bf,A.bg,A.at,A.bh])
r(A.aO,A.bs)
q(A.bF,[A.c5,A.c6,A.cr,A.c8,A.cf,A.ce,A.cb,A.ca,A.c9,A.ci,A.ch,A.cg,A.bU,A.co,A.cl,A.cz,A.cn])
r(A.aM,A.a3)
r(A.X,A.aM)
r(A.aA,A.X)
r(A.a6,A.W)
r(A.aB,A.a6)
r(A.aN,A.a5)
r(A.a4,A.bv)
r(A.a7,A.br)
r(A.cm,A.cv)
r(A.aD,A.a1)
r(A.aF,A.aD)
q(A.A,[A.aw,A.b1])
s(A.aG,A.j)
s(A.aH,A.ai)
s(A.aI,A.j)
s(A.aJ,A.ai)
s(A.a4,A.bp)})()
var v={typeUniverse:{eC:new Map(),tR:{},eT:{},tPV:{},sEA:[]},mangledGlobalNames:{a:"int",h:"double",h_:"num",V:"String",fK:"bool",q:"Null",i:"List",d:"Object",h8:"Map"},mangledNames:{},types:["~()","~(~())","q(@)","q()","~(@)","~(d,y)","@(@)","@(@,V)","@(V)","q(~())","q(@,y)","~(a,@)","q(d,y)","p<@>(@)","~(d?,d?)","d?(d?)","~(d)","q(n)"],interceptorsByTag:null,leafTags:null,arrayRti:Symbol("$ti")}
A.eZ(v.typeUniverse,JSON.parse('{"bi":"M","ay":"M","L":"M","b3":{"f":[]},"ak":{"q":[],"f":[]},"an":{"n":[]},"M":{"n":[]},"u":{"i":["1"],"e":["1"],"n":[],"c":["1"]},"bO":{"u":["1"],"i":["1"],"e":["1"],"n":[],"c":["1"]},"b5":{"h":[]},"aj":{"h":[],"a":[],"f":[]},"b4":{"h":[],"f":[]},"al":{"V":[],"f":[]},"ap":{"l":[]},"e":{"c":["1"]},"N":{"e":["1"],"c":["1"]},"U":{"c":["2"],"c.E":"2"},"ag":{"U":["1","2"],"e":["2"],"c":["2"],"c.E":"2"},"D":{"N":["2"],"e":["2"],"c":["2"],"c.E":"2","N.E":"2"},"au":{"F":[],"l":[]},"b6":{"l":[]},"bm":{"l":[]},"aL":{"y":[]},"bq":{"l":[]},"bj":{"l":[]},"b8":{"n":[],"cN":[],"f":[]},"as":{"n":[]},"b9":{"cO":[],"n":[],"f":[]},"a2":{"v":["1"],"n":[]},"aq":{"j":["h"],"i":["h"],"v":["h"],"e":["h"],"n":[],"c":["h"]},"ar":{"j":["a"],"i":["a"],"v":["a"],"e":["a"],"n":[],"c":["a"]},"ba":{"bJ":[],"j":["h"],"i":["h"],"v":["h"],"e":["h"],"n":[],"c":["h"],"f":[],"j.E":"h"},"bb":{"bK":[],"j":["h"],"i":["h"],"v":["h"],"e":["h"],"n":[],"c":["h"],"f":[],"j.E":"h"},"bc":{"bL":[],"j":["a"],"i":["a"],"v":["a"],"e":["a"],"n":[],"c":["a"],"f":[],"j.E":"a"},"bd":{"bM":[],"j":["a"],"i":["a"],"v":["a"],"e":["a"],"n":[],"c":["a"],"f":[],"j.E":"a"},"be":{"bN":[],"j":["a"],"i":["a"],"v":["a"],"e":["a"],"n":[],"c":["a"],"f":[],"j.E":"a"},"bf":{"bY":[],"j":["a"],"i":["a"],"v":["a"],"e":["a"],"n":[],"c":["a"],"f":[],"j.E":"a"},"bg":{"bZ":[],"j":["a"],"i":["a"],"v":["a"],"e":["a"],"n":[],"c":["a"],"f":[],"j.E":"a"},"at":{"c_":[],"j":["a"],"i":["a"],"v":["a"],"e":["a"],"n":[],"c":["a"],"f":[],"j.E":"a"},"bh":{"c0":[],"j":["a"],"i":["a"],"v":["a"],"e":["a"],"n":[],"c":["a"],"f":[],"j.E":"a"},"bs":{"l":[]},"aO":{"F":[],"l":[]},"p":{"a_":["1"]},"S":{"l":[]},"aA":{"X":["1"],"a3":["1"]},"aB":{"W":["1"]},"aN":{"a5":["1"]},"a4":{"bv":["1"]},"X":{"a3":["1"]},"a6":{"W":["1"]},"aM":{"a3":["1"]},"aD":{"a1":["1","2"]},"aF":{"aD":["1","2"],"a1":["1","2"]},"aE":{"e":["1"],"c":["1"],"c.E":"1"},"aY":{"l":[]},"F":{"l":[]},"A":{"l":[]},"aw":{"l":[]},"b1":{"l":[]},"az":{"l":[]},"bl":{"l":[]},"E":{"l":[]},"b0":{"l":[]},"ax":{"l":[]},"bx":{"y":[]},"bN":{"i":["a"],"e":["a"],"c":["a"]},"c0":{"i":["a"],"e":["a"],"c":["a"]},"c_":{"i":["a"],"e":["a"],"c":["a"]},"bL":{"i":["a"],"e":["a"],"c":["a"]},"bY":{"i":["a"],"e":["a"],"c":["a"]},"bM":{"i":["a"],"e":["a"],"c":["a"]},"bZ":{"i":["a"],"e":["a"],"c":["a"]},"bJ":{"i":["h"],"e":["h"],"c":["h"]},"bK":{"i":["h"],"e":["h"],"c":["h"]}}'))
A.eY(v.typeUniverse,JSON.parse('{"e":1,"ai":1,"a2":1,"W":1,"aB":1,"bp":1,"a6":1,"aM":1,"br":1,"a7":1,"aK":1,"aC":1,"bw":1}'))
var u={g:"Cannot fire new event. Controller is already firing an event",c:"Error handler must accept one Object or one Object and a StackTrace as arguments, and return a value of the returned future's type"}
var t=(function rtii(){var s=A.dW
return{J:s("cN"),Y:s("cO"),V:s("e<@>"),Q:s("l"),B:s("bJ"),q:s("bK"),Z:s("h7"),O:s("bL"),k:s("bM"),U:s("bN"),x:s("c<d?>"),s:s("u<V>"),b:s("u<@>"),T:s("ak"),m:s("n"),g:s("L"),p:s("v<@>"),j:s("i<@>"),P:s("q"),K:s("d"),L:s("h9"),l:s("y"),N:s("V"),R:s("f"),c:s("F"),D:s("bY"),w:s("bZ"),e:s("c_"),E:s("c0"),o:s("ay"),I:s("a4<@>"),d:s("p<@>"),a:s("p<a>"),F:s("aF<d?,d?>"),y:s("fK"),i:s("h"),z:s("@"),v:s("@(d)"),C:s("@(d,y)"),S:s("a"),A:s("0&*"),_:s("d*"),W:s("a_<q>?"),X:s("d?"),H:s("h_"),n:s("~"),u:s("~(d)"),f:s("~(d,y)")}})();(function constants(){B.n=J.b2.prototype
B.o=J.u.prototype
B.p=J.aj.prototype
B.q=J.L.prototype
B.r=J.an.prototype
B.e=J.bi.prototype
B.b=J.ay.prototype
B.c=function getTagFallback(o) {
  var s = Object.prototype.toString.call(o);
  return s.substring(8, s.length - 1);
}
B.f=function() {
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
B.l=function(getTagFallback) {
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
B.h=function(hooks) {
  if (typeof dartExperimentalFixupGetTag != "function") return hooks;
  hooks.getTag = dartExperimentalFixupGetTag(hooks.getTag);
}
B.k=function(hooks) {
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
B.j=function(hooks) {
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
B.i=function(hooks) {
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
B.d=function(hooks) { return hooks; }

B.a=new A.cm()
B.m=new A.bx()
B.t=A.z("cN")
B.u=A.z("cO")
B.v=A.z("bJ")
B.w=A.z("bK")
B.x=A.z("bL")
B.y=A.z("bM")
B.z=A.z("bN")
B.A=A.z("n")
B.B=A.z("bY")
B.C=A.z("bZ")
B.D=A.z("c_")
B.E=A.z("c0")})();(function staticFields(){$.ck=null
$.w=A.bD([],A.dW("u<d>"))
$.dj=null
$.df=null
$.de=null
$.dY=null
$.dT=null
$.e1=null
$.cD=null
$.cH=null
$.d1=null
$.ab=null
$.aS=null
$.aT=null
$.cW=!1
$.k=B.a})();(function lazyInitializers(){var s=hunkHelpers.lazyFinal
s($,"h6","d8",()=>A.fO("_$dart_dartClosure"))
s($,"hb","e4",()=>A.G(A.bX({
toString:function(){return"$receiver$"}})))
s($,"hc","e5",()=>A.G(A.bX({$method$:null,
toString:function(){return"$receiver$"}})))
s($,"hd","e6",()=>A.G(A.bX(null)))
s($,"he","e7",()=>A.G(function(){var $argumentsExpr$="$arguments$"
try{null.$method$($argumentsExpr$)}catch(r){return r.message}}()))
s($,"hh","ea",()=>A.G(A.bX(void 0)))
s($,"hi","eb",()=>A.G(function(){var $argumentsExpr$="$arguments$"
try{(void 0).$method$($argumentsExpr$)}catch(r){return r.message}}()))
s($,"hg","e9",()=>A.G(A.dq(null)))
s($,"hf","e8",()=>A.G(function(){try{null.$method$}catch(r){return r.message}}()))
s($,"hk","ed",()=>A.G(A.dq(void 0)))
s($,"hj","ec",()=>A.G(function(){try{(void 0).$method$}catch(r){return r.message}}()))
s($,"hl","d9",()=>A.eD())})();(function nativeSupport(){!function(){var s=function(a){var m={}
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
hunkHelpers.setOrUpdateInterceptorsByTag({ArrayBuffer:A.b8,ArrayBufferView:A.as,DataView:A.b9,Float32Array:A.ba,Float64Array:A.bb,Int16Array:A.bc,Int32Array:A.bd,Int8Array:A.be,Uint16Array:A.bf,Uint32Array:A.bg,Uint8ClampedArray:A.at,CanvasPixelArray:A.at,Uint8Array:A.bh})
hunkHelpers.setOrUpdateLeafTags({ArrayBuffer:true,ArrayBufferView:false,DataView:true,Float32Array:true,Float64Array:true,Int16Array:true,Int32Array:true,Int8Array:true,Uint16Array:true,Uint32Array:true,Uint8ClampedArray:true,CanvasPixelArray:true,Uint8Array:false})
A.a2.$nativeSuperclassTag="ArrayBufferView"
A.aG.$nativeSuperclassTag="ArrayBufferView"
A.aH.$nativeSuperclassTag="ArrayBufferView"
A.aq.$nativeSuperclassTag="ArrayBufferView"
A.aI.$nativeSuperclassTag="ArrayBufferView"
A.aJ.$nativeSuperclassTag="ArrayBufferView"
A.ar.$nativeSuperclassTag="ArrayBufferView"})()
Function.prototype.$1=function(a){return this(a)}
Function.prototype.$0=function(){return this()}
Function.prototype.$2=function(a,b){return this(a,b)}
Function.prototype.$3=function(a,b,c){return this(a,b,c)}
Function.prototype.$4=function(a,b,c,d){return this(a,b,c,d)}
Function.prototype.$1$1=function(a){return this(a)}
convertAllToFastObject(w)
convertToFastObject($);(function(a){if(typeof document==="undefined"){a(null)
return}if(typeof document.currentScript!="undefined"){a(document.currentScript)
return}var s=document.scripts
function onLoad(b){for(var q=0;q<s.length;++q){s[q].removeEventListener("load",onLoad,false)}a(b.target)}for(var r=0;r<s.length;++r){s[r].addEventListener("load",onLoad,false)}})(function(a){v.currentScript=a
var s=A.d3
if(typeof dartMainRunner==="function"){dartMainRunner(s,[])}else{s([])}})})()
//# sourceMappingURL=worker.dart.js.map
