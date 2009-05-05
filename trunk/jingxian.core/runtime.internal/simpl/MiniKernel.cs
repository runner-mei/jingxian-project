using System;
using System.Collections.Generic;
using System.Text;
using System.Reflection;

namespace jingxian.core.runtime.simpl
{
    using jingxian.core.runtime.simpl.mini;

    public class MiniKernel : IKernel
    {
        private string _id;
        private IKernel _parent;

        InstanceMap _instanceMap = new InstanceMap();
        ComponentMap _componentMap = new ComponentMap();

        bool _isStarted = false;

        public MiniKernel()
        : this(null, null )
        { 
        }

        public MiniKernel(string id, IKernel parent)
        {
            _parent = parent;
            _id = id;
            _instanceMap.Add(null
                , new Type[] { typeof(IKernel), typeof(ILocator), typeof(IServiceProvider) }
                , this);
        }

        #region IKernel 成员

        public bool Contains<T>()
        {
            return Contains( typeof(T) );
        }

        public bool Contains(string id)
        {
            if (_instanceMap.Contains(id))
                return true;
            if (null == _parent)
                return false;
            return _parent.Contains(id);
        }

        public bool Contains(Type service)
        {
            if (_instanceMap.Contains(service))
                return true;
            if (null == _parent)
                return false;
            return _parent.Contains(service);
        }

        public void Release(object instance)
        {

        }

        public IKernelBuilder CreateBuilder()
        {
            return new KernelBuilder(this);
        }

        public void Connect(string id
            , IEnumerable<Type> serviceTypes
            , Type classType
            , ComponentLifestyle lifestyle
            , IEnumerable<IParameter> parameters
            , IProperties properties)
        {
            if (string.IsNullOrEmpty(id))
                id = Guid.NewGuid().ToString();

            _componentMap.Add(new Descriptor(id, serviceTypes
                , classType, lifestyle, parameters, properties) );
        }

        public object Build(
              Type classType
            , IEnumerable<IParameter> parameters
            , IProperties properties)
        {
            Descriptor descriptor = new Descriptor("Transient", null
                , classType, ComponentLifestyle.Transient, parameters, properties);

            return CreateService(descriptor);
        }

        public bool Disconnect(string id)
        {
            _componentMap.Remove(id);
            return _instanceMap.Remove(id);
        }

        #endregion

        #region ILocator 成员

        public object GetService(Type serviceType)
        {
            object instance = null;
            if (_instanceMap.TryGetValue(serviceType, out instance))
                return instance;

            Descriptor descriptor = null;
            if (_componentMap.TryGetValue(serviceType, out descriptor))
                return CreateService(descriptor);

            return (null == _parent) ? null : _parent.GetService(serviceType);
        }

        public object GetService(string serviceId)
        {
            object instance = null;
            if (_instanceMap.TryGetValue(serviceId, out instance))
                return instance;

            Descriptor descriptor = null;
            if (_componentMap.TryGetValue(serviceId, out descriptor))
                return CreateService(descriptor);

            return (null == _parent) ? null : _parent.GetService(serviceId);
        }

        public object this[Type serviceType]
        {
            get { return GetService( serviceType ); }
        }

        public object this[string key]
        {
            get { return GetService( key ); }
        }

        public T Get<T>()
        {
            return (T)GetService(typeof(T));
        }

        public T Get<T>(string id)
        {
            return (T)Get(id, typeof(T) );
        }

        public object Get(string id, Type service)
        {
            object value = GetService(id);
            if (null == value)
                return value;
            if (service.IsAssignableFrom(value.GetType()))
                return value;
            return null;
        }

        #endregion

        #region Kernel Starter 成员

        public void Start()
        {
            if (_isStarted)
                return;

            foreach (KeyValuePair<string, Descriptor> kp in _componentMap)
            {
                if (ComponentLifestyle.Singleton == kp.Value.Lifestyle)
                {
                    object instance = null;
                    if (!_instanceMap.TryGetValue(kp.Value.Id, out instance))
                        instance = CreateService(kp.Value);
                }
            }

            foreach (KeyValuePair<string, object> kp in _instanceMap)
            {
                    Start(this, kp.Value);
            }

            _isStarted = true;
        }

        public void Stop()
        {
            if (!_isStarted)
                return;

            foreach (KeyValuePair<string, object> kp in _instanceMap)
            {
                Stop(this, kp.Value);
            }

            _isStarted = false;
        }

        public void Start(IKernel kernel, object instance)
        {
            invokeMethod("Start", kernel, instance);
        }

        public static void Stop(IKernel kernel, object instance)
        {
            invokeMethod("Stop", kernel, instance);
        }

        static void invokeMethod(string methodName, IKernel kernel, object instance)
        {
            MethodInfo methodInfo = instance.GetType().GetMethod(methodName);
            if (null == methodInfo)
                return;

            ParameterInfo[] parameters = methodInfo.GetParameters();
            switch (parameters.Length)
            {
                case 0:
                        methodInfo.Invoke(instance, new object[] { });
                        break;
                case 1:
                    if (parameters[0].ParameterType == typeof(IKernel))
                        methodInfo.Invoke(instance, new object[] { kernel });
                    break;
            }
        }


        protected object invokeConstructors( IComponentDescriptor descriptor)
        {
            ConstructorInfo[] constructors = descriptor.ImplementationType.GetConstructors();
            if (constructors.Length > 1)
                throw new RuntimeException(string.Format("不能创建服务[{0}]实例,类型[{1}]有多个构造函数", descriptor.Id, descriptor.ImplementationType.FullName));
            else if (constructors.Length != 1)
                throw new RuntimeException(string.Format("不能创建服务[{0}]实例,类型[{1}]没有构造函数", descriptor.Id, descriptor.ImplementationType.FullName));

            ConstructorInfo constructor = constructors[0];
            ParameterInfo[] constructorParameters = constructor.GetParameters();

            if (constructorParameters.Length == 0)
                return Activator.CreateInstance(descriptor.ImplementationType);

            object[] parameters = new object[constructorParameters.Length];

            for (int i = 0; i < constructorParameters.Length; i++)
                parameters[i] = GetService(constructorParameters[i].ParameterType);

            return constructor.Invoke(parameters);
        }

        protected void invokeSetters(object instance, IComponentDescriptor descriptor)
        {
            if (null == instance)
                return;

            foreach (PropertyInfo propertyInfo in instance.GetType().GetProperties())
            {
                if (!propertyInfo.CanWrite || null == propertyInfo.GetSetMethod())
                    continue;

                Type propertyType = propertyInfo.PropertyType;

                try
                {
                    object val = GetService(propertyType);
                    if (null != val)
                        propertyInfo.SetValue(instance, val, null);
                }
                catch (Exception exception)
                {
                    throw new RuntimeException(string.Format("创建服务[ " + propertyType.Name + "]失败", exception));
                }
            }
        }

        protected object CreateService(IComponentDescriptor descriptor)
        {
            object instance = invokeConstructors(descriptor);
            invokeSetters(instance, descriptor);

            if (ComponentLifestyle.Singleton == descriptor.Lifestyle
                || ComponentLifestyle.DependencyInjectionOnly == descriptor.Lifestyle)
                _instanceMap.Add(descriptor.Id, descriptor.Services, instance);

            return instance;
        }

        #endregion

        #region IDisposable 成员

        public void Dispose()
        {
        }

        #endregion
    }
}
