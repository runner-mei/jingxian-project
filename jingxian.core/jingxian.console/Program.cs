﻿using System;
using System.Collections.Generic;
using System.Text;

namespace jingxian.console
{

    using jingxian.core.runtime;
    using jingxian.core.runtime.simpl;

    class Program
    {
        static void Main(string[] args)
        {
            ApplicationContext context = new ApplicationContext();
            context.ApplicationLaunchableId = "jingxian.core.testSupport.serviceLauncher";

            jingxian.core.runtime.simpl.Platform.Launch(context, null);
        }
    }
}
