﻿
using System;


namespace jingxian.core.runtime
{
    using jingxian.core.runtime.Xml.Serialization;

	[ExtensionContract]
	public abstract class ConfigurationElement: XmlSerializableIdentifiable, IConfigurationElement
	{
		private IExtension _declaringExtension;

		protected ConfigurationElement()
		{
		}

		protected ConfigurationElement(string id)
			: base(id)
		{
		}

		public IExtension DeclaringExtension
		{
            get { return _declaringExtension; }
		}

        void IConfigurationElement.Configure(IExtension declaringExtension)
        {
            Enforce.ArgumentNotNull<IExtension>(declaringExtension,
                "declaringExtension");

            if (_declaringExtension != null)
                throw new WriteOnceViolatedException("declaringExtension");

            _declaringExtension = declaringExtension;
        }

	}
}